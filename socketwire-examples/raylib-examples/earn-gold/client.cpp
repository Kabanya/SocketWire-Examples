#include <cstdio>
#include <unordered_map>
#include <memory>

// Prevent Windows GDI conflicts with raylib
#if defined(_WIN32)
    #define NOGDI
    #define NOUSER
#endif

#include "raylib.h"

#if defined(_WIN32)
    #undef DrawText
    #undef Rectangle
#endif
#include "protocol.h"
#include "i_socket.hpp"
#include "socket_init.hpp"
#include "reliable_connection.hpp"
#include "socket_poller.hpp"

const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;
const float PLAYER_SPEED = 200.0f;
const float PLAYER_SIZE = 20.0f;
const float COIN_SIZE = 10.0f;

struct Player
{
  uint16_t id;
  float x;
  float y;
  int score;
  Color color;
};

struct Coin
{
  uint16_t id;
  float x;
  float y;
  bool active;
};

static std::unordered_map<uint16_t, Player> players;
static std::unordered_map<uint16_t, Coin> coins;
static uint16_t myPlayerId = 0;

Color uint32ToColor(uint32_t colorValue)
{
  Color c;
  c.r = (colorValue >> 24) & 0xFF;
  c.g = (colorValue >> 16) & 0xFF;
  c.b = (colorValue >> 8) & 0xFF;
  c.a = colorValue & 0xFF;
  return c;
}

class ClientHandler : public socketwire::IReliableConnectionHandler
{
public:
  void onConnected() override
  {
    printf("[CLIENT] Connected to server!\n");
    fflush(stdout);
    connected = true;
  }

  void onDisconnected() override
  {
    printf("[CLIENT] Disconnected from server\n");
    fflush(stdout);
    connected = false;
  }

  void onReliableReceived([[maybe_unused]] std::uint8_t channel, const void* data, std::size_t size) override
  {
    processPacket(data, size);
  }

  void onUnreliableReceived([[maybe_unused]] std::uint8_t channel, const void* data, std::size_t size) override
  {
    processPacket(data, size);
  }

  void onTimeout() override
  {
    printf("[CLIENT] Connection timeout\n");
    fflush(stdout);
  }

  bool isConnected() const { return connected; }

private:
  bool connected = false;

  void processPacket(const void* data, std::size_t size)
  {
    MessageType msgType = get_packet_type(data, size);

    switch (msgType)
    {
    case E_SERVER_TO_CLIENT_WELCOME:
    {
      deserialize_welcome(data, size, myPlayerId);
      printf("[CLIENT] Received welcome! My player ID: %d\n", myPlayerId);
      fflush(stdout);
      break;
    }

    case E_SERVER_TO_CLIENT_PLAYER_JOINED:
    {
      uint16_t playerId;
      float x, y;
      uint32_t colorValue;
      deserialize_player_joined(data, size, playerId, x, y, colorValue);

      Player player;
      player.id = playerId;
      player.x = x;
      player.y = y;
      player.score = 0;
      player.color = uint32ToColor(colorValue);

      players[playerId] = player;
      printf("[CLIENT] Player %d joined at (%.1f, %.1f)\n", playerId, x, y);
      fflush(stdout);
      break;
    }

    case E_SERVER_TO_CLIENT_PLAYER_LEFT:
    {
      uint16_t playerId;
      deserialize_player_left(data, size, playerId);
      players.erase(playerId);
      printf("[CLIENT] Player %d left\n", playerId);
      fflush(stdout);
      break;
    }

    case E_SERVER_TO_CLIENT_PLAYER_UPDATE:
    {
      uint16_t playerId;
      float x, y;
      deserialize_player_update(data, size, playerId, x, y);

      auto it = players.find(playerId);
      if (it != players.end())
      {
        it->second.x = x;
        it->second.y = y;
      }
      break;
    }

    case E_SERVER_TO_CLIENT_COIN_SPAWN:
    {
      uint16_t coinId;
      float x, y;
      deserialize_coin_spawn(data, size, coinId, x, y);

      Coin coin;
      coin.id = coinId;
      coin.x = x;
      coin.y = y;
      coin.active = true;

      coins[coinId] = coin;
      printf("[CLIENT] Coin %d spawned at (%.1f, %.1f)\n", coinId, x, y);
      fflush(stdout);
      break;
    }

    case E_SERVER_TO_CLIENT_COIN_COLLECTED:
    {
      uint16_t coinId, playerId;
      deserialize_coin_collected(data, size, coinId, playerId);

      auto it = coins.find(coinId);
      if (it != coins.end())
      {
        it->second.active = false;
      }

      printf("[CLIENT] Player %d collected coin %d\n", playerId, coinId);
      fflush(stdout);
      break;
    }

    case E_SERVER_TO_CLIENT_SCORE_UPDATE:
    {
      uint16_t playerId;
      int score;
      deserialize_score_update(data, size, playerId, score);

      auto it = players.find(playerId);
      if (it != players.end())
      {
        it->second.score = score;
      }

      printf("[CLIENT] Player %d score: %d\n", playerId, score);
      fflush(stdout);
      break;
    }

    default:
      break;
    }
  }
};

int main()
{
  printf("[CLIENT] Starting multiplayer client...\n");
  fflush(stdout);

  // Initialize SocketWire
  socketwire::initialize_sockets();
  auto* factory = socketwire::SocketFactoryRegistry::getFactory();
  if (factory == nullptr)
  {
    printf("[CLIENT] Cannot get socket factory\n");
    return 1;
  }

  // Create UDP socket
  socketwire::SocketConfig cfg;
  cfg.nonBlocking = true;
  cfg.reuseAddress = true;

  auto socket = factory->createUDPSocket(cfg);
  if (socket == nullptr)
  {
    printf("[CLIENT] Cannot create UDP socket\n");
    return 1;
  }

  // Bind to any port (client)
  socketwire::SocketAddress anyAddr = socketwire::SocketAddress::fromIPv4(0);
  auto bindResult = socket->bind(anyAddr, 0);
  if (bindResult != socketwire::SocketError::None)
  {
    printf("[CLIENT] Cannot bind socket\n");
    return 1;
  }

  // Create connection
  socketwire::ReliableConnectionConfig connCfg;
  connCfg.numChannels = 2;
  socketwire::ReliableConnection connection(socket.get(), connCfg);

  // Set up handler
  ClientHandler handler;
  connection.setHandler(&handler);

  // Connect to server
  socketwire::SocketAddress serverAddr = socketwire::SocketAddress::fromIPv4(0x7F000001); // 127.0.0.1
  connection.connect(serverAddr, 10131);

  printf("[CLIENT] Connecting to server at 127.0.0.1:10131...\n");
  fflush(stdout);

  // Create socket poller
  socketwire::SocketPoller poller;
  poller.addSocket(socket.get());

  // Initialize Raylib
  InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Coin Collector - Multiplayer");
  SetTargetFPS(60);

  bool sentJoin = false;

  while (!WindowShouldClose())
  {
    float dt = GetFrameTime();

    // Poll socket for incoming data
    auto events = poller.poll(0); // non-blocking
    for (auto& ev : events)
    {
      if (ev.readable)
      {
        socketwire::SocketAddress fromAddr;
        std::uint16_t fromPort = 0;
        char buffer[2048];

        auto result = socket->receive(buffer, sizeof(buffer), fromAddr, fromPort);
        if (result.succeeded() && result.bytes > 0)
        {
          connection.processPacket(buffer, static_cast<size_t>(result.bytes), fromAddr, fromPort);
        }
      }
    }

    // Update connection
    connection.update();

    // Send join when connected
    if (connection.isConnected() && !sentJoin)
    {
      printf("[CLIENT] Sending join request\n");
      fflush(stdout);
      send_join(&connection);
      sentJoin = true;
    }

    // Handle player input
    if (myPlayerId != 0)
    {
      auto it = players.find(myPlayerId);
      if (it != players.end())
      {
        Player& myPlayer = it->second;

        bool moved = false;
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))
        {
          myPlayer.x -= PLAYER_SPEED * dt;
          moved = true;
        }
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
        {
          myPlayer.x += PLAYER_SPEED * dt;
          moved = true;
        }
        if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))
        {
          myPlayer.y -= PLAYER_SPEED * dt;
          moved = true;
        }
        if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))
        {
          myPlayer.y += PLAYER_SPEED * dt;
          moved = true;
        }

        // Keep player in bounds
        if (myPlayer.x < PLAYER_SIZE) myPlayer.x = PLAYER_SIZE;
        if (myPlayer.x > SCREEN_WIDTH - PLAYER_SIZE) myPlayer.x = SCREEN_WIDTH - PLAYER_SIZE;
        if (myPlayer.y < PLAYER_SIZE) myPlayer.y = PLAYER_SIZE;
        if (myPlayer.y > SCREEN_HEIGHT - PLAYER_SIZE) myPlayer.y = SCREEN_HEIGHT - PLAYER_SIZE;

        // Send position to server if moved
        if (moved)
        {
          send_player_move(&connection, myPlayer.x, myPlayer.y);
        }
      }
    }

    // Render
    BeginDrawing();
    ClearBackground(Color{30, 30, 40, 255});

    // Draw coins
    for (const auto& pair : coins)
    {
      if (pair.second.active)
      {
        DrawCircle(static_cast<int>(pair.second.x),
                  static_cast<int>(pair.second.y),
                  COIN_SIZE,
                  GOLD);
        DrawCircleLines(static_cast<int>(pair.second.x),
                       static_cast<int>(pair.second.y),
                       COIN_SIZE,
                       YELLOW);
      }
    }

    // Draw players
    for (const auto& pair : players)
    {
      const Player& player = pair.second;
      bool isMe = (player.id == myPlayerId);

      DrawCircle(static_cast<int>(player.x),
                static_cast<int>(player.y),
                PLAYER_SIZE,
                player.color);
      DrawCircleLines(static_cast<int>(player.x),
                     static_cast<int>(player.y),
                     PLAYER_SIZE,
                     isMe ? LIME : SKYBLUE);

      // Draw player ID
      char idText[10];
      snprintf(idText, sizeof(idText), "%d", player.id);
      DrawText(idText, static_cast<int>(player.x) - 10, static_cast<int>(player.y) - 10, 10, WHITE);
    }

    // Draw UI
    if (myPlayerId != 0)
    {
      auto it = players.find(myPlayerId);
      if (it != players.end())
      {
        char scoreText[50];
        snprintf(scoreText, sizeof(scoreText), "Your Score: %d", it->second.score);
        DrawText(scoreText, 10, 10, 30, WHITE);

        char idText[50];
        snprintf(idText, sizeof(idText), "Player ID: %d", myPlayerId);
        DrawText(idText, 10, 45, 20, LIGHTGRAY);
      }
    }
    else if (connection.isConnected())
    {
      DrawText("Waiting to join...", 10, 10, 30, YELLOW);
    }
    else
    {
      DrawText("Connecting to server...", 10, 10, 30, RED);
    }

    // Draw player count
    char playerCount[50];
    snprintf(playerCount, sizeof(playerCount), "Players: %zu", players.size());
    DrawText(playerCount, SCREEN_WIDTH - 150, 10, 20, WHITE);

    // Draw instructions
    DrawText("Use WASD or Arrow Keys to move", 10, SCREEN_HEIGHT - 30, 20, LIGHTGRAY);

    EndDrawing();
  }

  CloseWindow();
  return 0;
}