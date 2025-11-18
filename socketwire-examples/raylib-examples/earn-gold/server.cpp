#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <thread>

#include "protocol.h"
#include "i_socket.hpp"
#include "reliable_connection.hpp"
#include "socket_poller.hpp"

const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;
const float PLAYER_SIZE = 20.0f;
const float COIN_SIZE = 10.0f;
const int MAX_COINS = 20;

struct Player
{
  uint16_t id;
  float x;
  float y;
  int score;
  uint32_t color;
  socketwire::ConnectionManager::RemoteClient* client;
};

struct Coin
{
  uint16_t id;
  float x;
  float y;
  bool active;
};

static std::unordered_map<uint16_t, Player> players;
static std::vector<Coin> coins;
static uint16_t nextPlayerId = 1;
static uint16_t nextCoinId = 1;

uint32_t randomColor()
{
  uint8_t r = 100 + rand() % 156;
  uint8_t g = 100 + rand() % 156;
  uint8_t b = 100 + rand() % 156;
  return (r << 24) | (g << 16) | (b << 8) | 0xFF;
}

void spawnCoin(Coin& coin)
{
  coin.x = static_cast<float>(50 + rand() % (SCREEN_WIDTH - 100));
  coin.y = static_cast<float>(50 + rand() % (SCREEN_HEIGHT - 100));
  coin.active = true;
}

void initCoins()
{
  coins.resize(MAX_COINS);
  for (int i = 0; i < MAX_COINS; i++)
  {
    coins[i].id = nextCoinId++;
    spawnCoin(coins[i]);
  }
}

bool checkCollision(float x1, float y1, float r1, float x2, float y2, float r2)
{
  float dx = x1 - x2;
  float dy = y1 - y2;
  float distance = dx * dx + dy * dy;
  float radiusSum = r1 + r2;
  return distance < radiusSum * radiusSum;
}

class ServerHandler : public socketwire::IReliableConnectionHandler
{
public:
  void onConnected() override
  {
    printf("[SERVER] Client connected\n");
    fflush(stdout);
  }

  void onDisconnected() override
  {
    printf("[SERVER] Client disconnected\n");
    fflush(stdout);
  }

  void onReliableReceived([[maybe_unused]] std::uint8_t channel, const void* data, std::size_t size) override
  {
    processPacket(data, size);
  }

  void onUnreliableReceived([[maybe_unused]] std::uint8_t channel, const void* data, std::size_t size) override
  {
    processPacket(data, size);
  }

  void setClient(socketwire::ConnectionManager::RemoteClient* c)
  {
    client = c;
  }

private:
  socketwire::ConnectionManager::RemoteClient* client = nullptr;

  void processPacket(const void* data, std::size_t size)
  {
    MessageType msgType = get_packet_type(data, size);

    switch (msgType)
    {
    case E_CLIENT_TO_SERVER_JOIN:
    {
      printf("[SERVER] Client requesting to join\n");
      fflush(stdout);

      // Create new player
      Player player;
      player.id = nextPlayerId++;
      player.x = SCREEN_WIDTH / 2.0f + (rand() % 100 - 50);
      player.y = SCREEN_HEIGHT / 2.0f + (rand() % 100 - 50);
      player.score = 0;
      player.color = randomColor();
      player.client = client;

      players[player.id] = player;

      // Send welcome to new player
      send_welcome(client->connection, player.id);

      // Send all existing players to new player (including self!)
      for (const auto& pair : players)
      {
        send_player_joined(client->connection, pair.second.id, pair.second.x, pair.second.y, pair.second.color);
        send_score_update(client->connection, pair.second.id, pair.second.score);
      }

      // Send all coins to new player
      for (const auto& coin : coins)
      {
        if (coin.active)
        {
          send_coin_spawn(client->connection, coin.id, coin.x, coin.y);
        }
      }

      // Notify all other players about new player
      for (const auto& pair : players)
      {
        if (pair.first != player.id && pair.second.client != nullptr)
        {
          send_player_joined(pair.second.client->connection, player.id, player.x, player.y, player.color);
        }
      }

      printf("[SERVER] Player %d joined at (%.1f, %.1f)\n", player.id, player.x, player.y);
      fflush(stdout);
      break;
    }

    case E_CLIENT_TO_SERVER_MOVE:
    {
      float x, y;
      deserialize_player_move(data, size, x, y);

      // Find player by client
      uint16_t playerId = 0;
      for (auto& pair : players)
      {
        if (pair.second.client == client)
        {
          playerId = pair.first;
          break;
        }
      }

      if (playerId == 0)
        break;

      Player& player = players[playerId];
      player.x = x;
      player.y = y;

      // Check coin collisions
      for (auto& coin : coins)
      {
        if (coin.active && checkCollision(player.x, player.y, PLAYER_SIZE, coin.x, coin.y, COIN_SIZE))
        {
          coin.active = false;
          player.score += 10;

          // Notify all players
          for (const auto& pair : players)
          {
            if (pair.second.client != nullptr)
            {
              send_coin_collected(pair.second.client->connection, coin.id, playerId);
              send_score_update(pair.second.client->connection, playerId, player.score);
            }
          }

          // Respawn coin
          spawnCoin(coin);

          // Notify all players about new coin
          for (const auto& pair : players)
          {
            if (pair.second.client != nullptr)
            {
              send_coin_spawn(pair.second.client->connection, coin.id, coin.x, coin.y);
            }
          }

          printf("[SERVER] Player %d collected coin %d, score: %d\n", playerId, coin.id, player.score);
          fflush(stdout);
        }
      }

      // Broadcast position to all other players
      for (const auto& pair : players)
      {
        if (pair.first != playerId && pair.second.client != nullptr)
        {
          send_player_update(pair.second.client->connection, playerId, x, y);
        }
      }
      break;
    }

    default:
      break;
    }
  }
};

int main()
{
  srand(static_cast<unsigned int>(time(nullptr)));

  printf("[SERVER] Starting multiplayer server...\n");
  fflush(stdout);

  // Initialize SocketWire
  socketwire::register_posix_socket_factory();
  auto* factory = socketwire::SocketFactoryRegistry::getFactory();
  if (factory == nullptr)
  {
    printf("[SERVER] Cannot get socket factory\n");
    return 1;
  }

  // Create UDP socket
  socketwire::SocketConfig cfg;
  cfg.nonBlocking = true;
  cfg.reuseAddress = true;

  auto socket = factory->createUDPSocket(cfg);
  if (socket == nullptr)
  {
    printf("[SERVER] Cannot create UDP socket\n");
    return 1;
  }

  // Bind to port 10131
  socketwire::SocketAddress anyAddr = socketwire::SocketAddress::fromIPv4(0);
  auto bindResult = socket->bind(anyAddr, 10131);
  if (bindResult != socketwire::SocketError::None)
  {
    printf("[SERVER] Cannot bind socket to port 10131\n");
    return 1;
  }

  printf("[SERVER] Socket bound to port 10131\n");
  fflush(stdout);

  // Create connection manager
  socketwire::ReliableConnectionConfig connCfg;
  connCfg.numChannels = 2;
  socketwire::ConnectionManager connManager(socket.get(), connCfg);

  ServerHandler handler;
  connManager.setHandler(&handler);

  // Initialize coins
  initCoins();

  // Create socket poller
  socketwire::SocketPoller poller;
  poller.addSocket(socket.get());

  printf("[SERVER] Server ready! Waiting for players...\n");
  fflush(stdout);

  auto lastUpdate = std::chrono::steady_clock::now();

  while (true)
  {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate).count();

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
          // Find or create client
          auto* client = connManager.getConnection(fromAddr, fromPort);
          if (client == nullptr)
          {
            printf("[SERVER] New connection from %u:%u\n", fromAddr.ipv4.hostOrderAddress, fromPort);
            fflush(stdout);
          }

          // Set handler for this specific client
          connManager.processPacket(buffer, static_cast<size_t>(result.bytes), fromAddr, fromPort);
          
          client = connManager.getConnection(fromAddr, fromPort);
          if (client != nullptr)
          {
            handler.setClient(client);
          }
        }
      }
    }

    // Update connection manager
    connManager.update();

    // Update at ~60 FPS
    if (elapsed >= 16)
    {
      lastUpdate = now;
    }
    else
    {
      // Sleep a bit to avoid busy-waiting
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  return 0;
}