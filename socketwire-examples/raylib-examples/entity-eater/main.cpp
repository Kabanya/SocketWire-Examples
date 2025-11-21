#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>

#include "windows_defines.hpp"  // IWYU pragma: keep

#include "raylib.h"

#include "entity.h"
#include "protocol.h"
#include "i_socket.hpp"
#include "socket_init.hpp"
#include "reliable_connection.hpp"
#include "socket_poller.hpp"

static std::vector<Entity> entities;
static std::unordered_map<uint16_t, size_t> indexMap;
static uint16_t myEntity = INVALID_ENTITY;

static int gameTimeRemaining = 60;
static bool gameOver = false;
static uint16_t winnerEid = INVALID_ENTITY;
static int winnerScore = 0;

void on_new_entity_packet(const void* data, size_t size)
{
  Entity newEntity;
  deserialize_new_entity(data, size, newEntity);
  auto itf = indexMap.find(newEntity.eid);
  if (itf != indexMap.end())
    return; // we don't do anything if there is an entity
  indexMap[newEntity.eid] = entities.size();
  entities.push_back(newEntity);
}

void on_set_controlled_entity(const void* data, size_t size)
{
  deserialize_set_controlled_entity(data, size, myEntity);
}

template<typename Callable>
static void get_entity(uint16_t eid, Callable c)
{
  auto itf = indexMap.find(eid);
  if (itf != indexMap.end())
    c(entities[itf->second]);
}

void on_snapshot(const void* data, size_t size)
{
  uint16_t eid = INVALID_ENTITY;
  float x = 0.f; float y = 0.f; float entity_size = 0.f;
  deserialize_snapshot(data, size, eid, x, y, entity_size);
  get_entity(eid, [&](Entity& e)
  {
    e.x = x;
    e.y = y;
    e.size = entity_size;
  });
}

void on_entity_devoured(const void* data, size_t size)
{
  uint16_t devouredEid = INVALID_ENTITY;
  uint16_t devourerEid = INVALID_ENTITY;
  float newSize = 0.f;
  float newX = 0.f;
  float newY = 0.f;

  deserialize_entity_devoured(data, size, devouredEid, devourerEid, newSize, newX, newY);

  get_entity(devourerEid, [&](Entity& e)
  {
    e.size = newSize;
  });

  get_entity(devouredEid, [&](Entity& e)
  {
    e.x = newX;
    e.y = newY;
  });
}

void on_score_update(const void* data, size_t size)
{
  uint16_t eid = INVALID_ENTITY;
  int score = 0;

  deserialize_score_update(data, size, eid, score);

  get_entity(eid, [&](Entity& e)
  {
    e.score = score;
  });
}

void on_game_time(const void* data, size_t size)
{
  int secondsRemaining = 0;
  deserialize_game_time(data, size, secondsRemaining);
  gameTimeRemaining = secondsRemaining;
}

void on_game_over(const void* data, size_t size)
{
  uint16_t wEid = INVALID_ENTITY;
  int wScore = 0;

  deserialize_game_over(data, size, wEid, wScore);

  gameOver = true;
  winnerEid = wEid;
  winnerScore = wScore;

  printf("Game Over! Winner is entity %d with score %d\n", winnerEid, winnerScore);
}

bool compare_entity_scores(const Entity& a, const Entity& b)
{
  return a.score > b.score;
}

// Client connection handler
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
    printf("[CLIENT] Received reliable packet, size=%zu\n", size);
    fflush(stdout);
    processPacket(data, size);
  }

  void onUnreliableReceived([[maybe_unused]] std::uint8_t channel, const void* data, std::size_t size) override
  {
    printf("[CLIENT] Received unreliable packet, size=%zu\n", size);
    fflush(stdout);
    processPacket(data, size);
  }

  void onTimeout() override
  {
    printf("[CLIENT] Connection timeout\n");
  }

  bool isConnected() const { return connected; }

private:
  bool connected = false;

  void processPacket(const void* data, std::size_t size)
  {
    MessageType msgType = get_packet_type(data, size);
    printf("[CLIENT] Processing packet type=%d\n", static_cast<int>(msgType));
    fflush(stdout);

    switch (msgType)
    {
    case E_SERVER_TO_CLIENT_NEW_ENTITY:
      on_new_entity_packet(data, size);
      printf("[CLIENT] Received new entity\n");
      break;
    case E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY:
      on_set_controlled_entity(data, size);
      printf("[CLIENT] Got controlled entity ID=%d\n", myEntity);
      break;
    case E_SERVER_TO_CLIENT_SNAPSHOT:
      on_snapshot(data, size);
      break;
    case E_SERVER_TO_CLIENT_ENTITY_DEVOURED:
      on_entity_devoured(data, size);
      printf("[CLIENT] Entity devoured event\n");
      break;
    case E_SERVER_TO_CLIENT_SCORE_UPDATE:
      on_score_update(data, size);
      break;
    case E_SERVER_TO_CLIENT_GAME_TIME:
      on_game_time(data, size);
      break;
    case E_SERVER_TO_CLIENT_GAME_OVER:
      on_game_over(data, size);
      break;
    case E_CLIENT_TO_SERVER_JOIN:
    case E_CLIENT_TO_SERVER_STATE:
      printf("[CLIENT] Warning: received client-to-server packet\n");
      break;
    }
  }
};

int main()
{
  // Initialize SocketWire
  socketwire::initialize_sockets();
  auto* factory = socketwire::SocketFactoryRegistry::getFactory();
  if (factory == nullptr)
  {
    printf("Cannot get socket factory\n");
    return 1;
  }

  // Create UDP socket
  socketwire::SocketConfig cfg;
  cfg.nonBlocking = true;
  cfg.reuseAddress = true;

  auto socket = factory->createUDPSocket(cfg);
  if (socket == nullptr)
  {
    printf("Cannot create UDP socket\n");
    return 1;
  }

  // Bind to any port (client)
  socketwire::SocketAddress anyAddr = socketwire::SocketAddress::fromIPv4(0);
  auto bindResult = socket->bind(anyAddr, 0);
  if (bindResult != socketwire::SocketError::None)
  {
    printf("Cannot bind socket\n");
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

  // Create socket poller
  socketwire::SocketPoller poller;
  poller.addSocket(socket.get());

  int width = 800;
  int height = 600;
  InitWindow(width, height, "Entity Eater - SocketWire");

  const int scrWidth = GetMonitorWidth(0);
  const int scrHeight = GetMonitorHeight(0);
  if (scrWidth < width || scrHeight < height)
  {
    width = std::min(scrWidth, width);
    height = std::min(scrHeight - 150, height);
    SetWindowSize(width, height);
  }

  Camera2D camera = { {0, 0}, {0, 0}, 0.f, 1.f };
  camera.target = Vector2{ 0.f, 0.f };
  camera.offset = Vector2{ width * 0.5f, height * 0.5f };
  camera.rotation = 0.f;
  camera.zoom = 1.f;

  SetTargetFPS(60);

  bool sentJoin = false;
  socketwire::ConnectionState lastLoggedState = socketwire::ConnectionState::Disconnected;

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

    // Update connection (handles retries, timeouts, etc.)
    connection.update();

    // Log connection state changes
    auto currentState = connection.getState();
    if (currentState != lastLoggedState)
    {
      printf("[CLIENT] Connection state changed: %d -> %d\n", 
             static_cast<int>(lastLoggedState), static_cast<int>(currentState));
      fflush(stdout);
      lastLoggedState = currentState;
    }

    // Send join when connected
    if (connection.isConnected() && !sentJoin)
    {
      printf("[CLIENT] Sending join request\n");
      fflush(stdout);
      send_join(&connection);
      sentJoin = true;
    }

    // Handle player input
    if (myEntity != INVALID_ENTITY)
    {
      bool left = IsKeyDown(KEY_LEFT);
      bool right = IsKeyDown(KEY_RIGHT);
      bool up = IsKeyDown(KEY_UP);
      bool down = IsKeyDown(KEY_DOWN);
      get_entity(myEntity, [&](Entity& e)
      {
        e.x += ((left ? -dt : 0.f) + (right ? +dt : 0.f)) * 100.f;
        e.y += ((up ? -dt : 0.f) + (down ? +dt : 0.f)) * 100.f;

        send_entity_state(&connection, myEntity, e.x, e.y);
        camera.target.x = e.x;
        camera.target.y = e.y;
      });
    }

    // Render
    BeginDrawing();
      ClearBackground(Color{40, 40, 40, 255});
      BeginMode2D(camera);
        for (const Entity &e : entities)
        {
          DrawCircle((int)e.x, (int)e.y, e.size, GetColor(e.color));

          char idText[10];
          snprintf(idText, sizeof(idText), "%d", e.eid);
          DrawText(idText, (int)(e.x - 10), (int)(e.y - 10), 10, WHITE);
        }
      EndMode2D();

      if (myEntity != INVALID_ENTITY)
      {
        get_entity(myEntity, [&](Entity& e)
        {
          char scoreText[50];
          snprintf(scoreText, sizeof(scoreText), "Your Score: %d", e.score);
          DrawText(scoreText, 10, 10, 20, WHITE);

          char sizeText[50];
          snprintf(sizeText, sizeof(sizeText), "Size: %.1f", e.size);
          DrawText(sizeText, 10, 40, 20, WHITE);
        });
      }

      // Display game timer & Leaderboard
      char timeText[50];
      snprintf(timeText, sizeof(timeText), "Time: %d", gameTimeRemaining);
      DrawText(timeText, width / 2 - 50, 10, 30, YELLOW);

      DrawRectangle(width - 200, 10, 190, 210, Color{0, 0, 0, 150});
      DrawText("LEADERBOARD", width - 190, 15, 20, YELLOW);

      std::vector<Entity> sortedEntities = entities;
      std::sort(sortedEntities.begin(), sortedEntities.end(), compare_entity_scores);

      int maxToShow = std::min(8, (int)sortedEntities.size());
      for (int i = 0; i < maxToShow; i++)
      {
        const Entity& e = sortedEntities[i];
        char playerText[100];
        const char* playerType = e.serverControlled ? "AI" : "Player";
        Color textColor = (e.eid == myEntity) ? GREEN : WHITE;

        snprintf(playerText, sizeof(playerText), "%d. %s %d - Score: %d",
                i + 1,
                playerType,
                e.eid,
                e.score);

        DrawText(playerText, width - 190, 45 + (i * 20), 15, textColor);
      }

      if (gameOver)
      {
        DrawRectangle(0, 0, width, height, Color{0, 0, 0, 200});

        DrawText("GAME OVER", width/2 - 150, height/2 - 100, 50, RED);

        std::string winnerType = "Unknown";
        Color winnerColor = WHITE;

        get_entity(winnerEid, [&](Entity& e) {
          winnerType = e.serverControlled ? "AI" : "Player";
          winnerColor = GetColor(e.color);
        });

        char winnerText[100];
        snprintf(winnerText, sizeof(winnerText), "Winner: %s %d", winnerType.c_str(), winnerEid);
        DrawText(winnerText, width/2 - 120, height/2, 30, winnerColor);

        char scoreText[50];
        snprintf(scoreText, sizeof(scoreText), "Final Score: %d", winnerScore);
        DrawText(scoreText, width/2 - 100, height/2 + 50, 30, YELLOW);
      }
    EndDrawing();
  }

  CloseWindow();

  return 0;
}