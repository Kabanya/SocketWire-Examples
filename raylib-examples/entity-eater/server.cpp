#include "entity.h"
#include "protocol.h"
#include <stdlib.h>
#include <vector>
#include <map>
#include <stdio.h>
#include <cmath>
#include <algorithm>
#include <memory>
#include <chrono>
#include <thread>

#include "i_socket.hpp"
#include "reliable_connection.hpp"
#include "socket_poller.hpp"

static std::vector<Entity> entities;
static std::map<uint16_t, socketwire::ConnectionManager::RemoteClient*> controlledMap;

float random_spawn(const float max_size = 10.f)
{
  return (rand() % 100 - 50) * max_size;
}

static uint16_t create_random_entity()
{
  uint16_t newEid = entities.size();
  uint32_t color = 0xff000000 +
                   0x00440000 * (1 + rand() % 4) +
                   0x00004400 * (1 + rand() % 4) +
                   0x00000044 * (1 + rand() % 4);
  float x = random_spawn();
  float y = random_spawn();
  float size = 5.f + (rand() % 6);   // Random size between 5 and 10

  Entity ent;
  ent.color = color;
  ent.x = x;
  ent.y = y;
  ent.eid = newEid;
  ent.serverControlled = false;
  ent.targetX = 0.f;
  ent.targetY = 0.f;
  ent.size = size;
  ent.score = 0;

  entities.push_back(ent);
  return newEid;
}

// Server handler for connection events
class ServerHandler : public socketwire::IReliableConnectionHandler
{
public:
  explicit ServerHandler(socketwire::ConnectionManager* mgr) : manager(mgr) {}

  void onConnected() override
  {
    printf("Client connected\n");
  }

  void onDisconnected() override
  {
    printf("Client disconnected\n");
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
    printf("Client connection timeout\n");
  }

private:
  [[maybe_unused]] socketwire::ConnectionManager* manager;

  void processPacket(const void* data, std::size_t size)
  {
    MessageType msgType = get_packet_type(data, size);

    switch (msgType)
    {
      case E_CLIENT_TO_SERVER_JOIN:
        // Handle join - will be processed in main loop
        break;
      case E_CLIENT_TO_SERVER_STATE:
        on_state(data, size);
        break;
      default:
        break;
    }
  }

  void on_state(const void* data, size_t size)
  {
    uint16_t eid = INVALID_ENTITY;
    float x = 0.f; float y = 0.f;
    deserialize_entity_state(data, size, eid, x, y);
    for (Entity &e : entities)
      if (e.eid == eid)
      {
        e.x = x;
        e.y = y;
      }
  }
};

void on_join([[maybe_unused]] const void* data, [[maybe_unused]] size_t size, socketwire::ConnectionManager::RemoteClient* client, socketwire::ConnectionManager* manager)
{
  // send all entities to new client
  for (const Entity &ent : entities)
    send_new_entity(client->connection, ent);

  // create new entity for this client
  uint16_t newEid = create_random_entity();
  const Entity& ent = entities[newEid];

  controlledMap[newEid] = client;

  // send info about new entity to everyone
  auto clients = manager->getConnections();
  for (auto* c : clients)
  {
    if (c->connection != nullptr && c->connection->isConnected())
      send_new_entity(c->connection, ent);
  }

  // send info about controlled entity to the new client
  send_set_controlled_entity(client->connection, newEid);
}

int main()
{
  // Initialize SocketWire
  socketwire::register_posix_socket_factory();
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

  // Bind to server port
  socketwire::SocketAddress anyAddr = socketwire::SocketAddress::fromIPv4(0);
  auto bindResult = socket->bind(anyAddr, 10131);
  if (bindResult != socketwire::SocketError::None)
  {
    printf("Cannot bind socket to port 10131\n");
    return 1;
  }

  printf("Server listening on port 10131...\n");

  // Create connection manager
  socketwire::ReliableConnectionConfig connCfg;
  connCfg.numChannels = 2;
  socketwire::ConnectionManager manager(socket.get(), connCfg);

  // Set up handler
  ServerHandler handler(&manager);
  manager.setHandler(&handler);

  // Create socket poller
  socketwire::SocketPoller poller;
  poller.addSocket(socket.get());

  bool created_ai_entities = false;
  constexpr int NUM_AI = 10;

  const int GAME_DURATION = 60;
  int game_time_remaining = GAME_DURATION;
  auto last_time_update = std::chrono::steady_clock::now();
  bool game_over = false;

  auto lastTime = std::chrono::steady_clock::now();

  // Track pending join requests
  std::map<socketwire::ConnectionManager::RemoteClient*, bool> pendingJoins;

  while (true)
  {
    auto curTime = std::chrono::steady_clock::now();
    float dt = std::chrono::duration_cast<std::chrono::milliseconds>(curTime - lastTime).count() * 0.001f;
    lastTime = curTime;

    // Game timer update
    if (!game_over && created_ai_entities)
    {
      auto timeSinceUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(curTime - last_time_update).count();
      if (timeSinceUpdate >= 1000)
      {
        game_time_remaining--;
        last_time_update = curTime;

        // Send time updates to all clients
        auto clients = manager.getConnections();
        for (auto* client : clients)
        {
          if (client->connection != nullptr && client->connection->isConnected())
            send_game_time(client->connection, game_time_remaining);
        }

        printf("Game time remaining: %d seconds\n", game_time_remaining);

        if (game_time_remaining <= 0)
        {
          game_over = true;

          // Find highest score
          uint16_t winner_eid = INVALID_ENTITY;
          int highest_score = -1;

          for (const Entity &e : entities)
          {
            if (e.score > highest_score)
            {
              highest_score = e.score;
              winner_eid = e.eid;
            }
          }

          printf("Game over! Winner is entity %d with score %d\n", 
                 winner_eid, highest_score);

          for (auto* client : clients)
          {
            if (client->connection != nullptr && client->connection->isConnected())
              send_game_over(client->connection, winner_eid, highest_score);
          }
        }
      }
    }

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
          // Process packet through connection manager
          manager.processPacket(buffer, static_cast<size_t>(result.bytes), fromAddr, fromPort);

          // Check if this is a join request
          MessageType msgType = get_packet_type(buffer, static_cast<size_t>(result.bytes));
          if (msgType == E_CLIENT_TO_SERVER_JOIN)
          {
            auto* client = manager.getConnection(fromAddr, fromPort);
            if (client != nullptr && client->connection != nullptr)
            {
              // Create AI entities on first connection
              if (!created_ai_entities)
              {
                printf("Creating AI entities for first client\n");
                for (int i = 0; i < NUM_AI; ++i)
                {
                  uint16_t eid = create_random_entity();
                  entities[eid].serverControlled = true;
                  entities[eid].score = 0;
                  controlledMap[eid] = nullptr;
                }
                created_ai_entities = true;
              }

              // Mark connection as established
              client->connection->setConnected();
              // Handle join
              on_join(buffer, static_cast<size_t>(result.bytes), client, &manager);
            }
          }
        }
      }
    }

    // Update all connections
    manager.update();

    // AI movement
    for (Entity &e : entities)
    {
      if (e.serverControlled)
      {
        const float diffX = e.targetX - e.x;
        const float diffY = e.targetY - e.y;
        const float dirX = diffX > 0.f ? 1.f : -1.f;
        const float dirY = diffY > 0.f ? 1.f : -1.f;
        constexpr float SPD = 50.f;
        e.x += dirX * SPD * dt;
        e.y += dirY * SPD * dt;
        if (fabsf(diffX) < 10.f && fabsf(diffY) < 10.f)
        {
          e.targetX = random_spawn();
          e.targetY = random_spawn();
        }
      }
    }

    // Collision detection
    bool collision_occurred = false;
    for (size_t i = 0; i < entities.size() && !collision_occurred; i++)
    {
      for (size_t j = 0; j < entities.size(); j++)
      {
        if (i == j) continue;

        Entity &e1 = entities[i];
        Entity &e2 = entities[j];

        if (e1.size <= 0 || e2.size <= 0 || e1.size > 1000 || e2.size > 1000)
        {
          continue;
        }

        float dx = e1.x - e2.x;
        float dy = e1.y - e2.y;
        float distance = sqrt(dx*dx + dy*dy);

        if (distance < (e1.size + e2.size) && e1.size != e2.size && distance > 0.1f)
        {
          printf("Collision detected between entities %d (size %.1f) and %d (size %.1f)! Distance: %.1f < %.1f\n", 
                 e1.eid, e1.size, e2.eid, e2.size, distance, (e1.size + e2.size));

          Entity *devourer = nullptr;
          Entity *devoured = nullptr;

          if (e1.size > e2.size)
          {
            devourer = &e1;
            devoured = &e2;
          }
          else
          {
            devourer = &e2;
            devoured = &e1;
          }

          printf("Entity %d (size %.1f) devours Entity %d (size %.1f)\n",
                 devourer->eid, devourer->size, devoured->eid, devoured->size);

          float size_gain = devoured->size / 2.0f;

          if (size_gain > 0.0f && size_gain < 50.0f)
          {
            const float MAX_SIZE = 100.0f;
            float newSize = devourer->size + size_gain;
            devourer->size = std::min(newSize, MAX_SIZE);

            devoured->size = 5.0f + (rand() % 5); // Random size between 5 and 10

            if (!devoured->serverControlled)
            {
              devoured->score = 0;
            }

            if (!devourer->serverControlled)
            {
              devourer->score += static_cast<int>(size_gain);
            }
            else
            {
              devourer->score += static_cast<int>(size_gain);
            }

            // Send score update to all clients
            auto clients = manager.getConnections();
            for (auto* client : clients)
            {
              if (client->connection != nullptr && client->connection->isConnected())
                send_score_update(client->connection, devourer->eid, devourer->score);
            }

            devoured->x = (rand() % 100 - 50) * 10.f;
            devoured->y = (rand() % 100 - 50) * 10.f;

            // Send devoured event to all clients
            for (auto* client : clients)
            {
              if (client->connection != nullptr && client->connection->isConnected())
              {
                send_entity_devoured(client->connection, devoured->eid, devourer->eid, 
                                    devourer->size, devoured->x, devoured->y);
              }
            }

            collision_occurred = true;
          }
          else
          {
            printf("Warning: Invalid size gain (%.1f) detected! Skipping this collision.\n", size_gain);
          }
          break;
        }
      }
    }

    // Send snapshots to all clients
    auto clients = manager.getConnections();
    for (const Entity &e : entities)
    {
      for (auto* client : clients)
      {
        if (client->connection != nullptr && client->connection->isConnected())
        {
          // Don't send snapshot for entity controlled by this client
          if (controlledMap[e.eid] != client)
            send_snapshot(client->connection, e.eid, e.x, e.y, e.size);
        }
      }
    }

    // Small sleep to avoid spinning CPU
    std::this_thread::sleep_for(std::chrono::microseconds(1000));
  }

  return 0;
}