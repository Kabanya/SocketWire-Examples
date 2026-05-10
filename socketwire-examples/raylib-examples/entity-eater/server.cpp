#include "entity.h"
#include "protocol.h"

#include "benchmark_utils.hpp"
#include "server_connection_hub.hpp"
#include "socketwire_example_utils.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <thread>
#include <vector>

static std::vector<Entity> entities;
static std::map<std::uint16_t, socketwire_examples::ServerConnectionHub::Client*> controlledMap;

static std::mt19937& random_generator()
{
  static std::random_device random_device;
  static std::mt19937 generator(random_device());
  return generator;
}

static int random_int(int min, int max)
{
  std::uniform_int_distribution<int> distribution(min, max);
  return distribution(random_generator());
}

static float random_spawn(const float max_size = 10.f)
{
  return static_cast<float>(random_int(-50, 49)) * max_size;
}

static std::uint16_t create_random_entity()
{
  const auto newEid = static_cast<std::uint16_t>(entities.size());
  const std::uint32_t color = 0xff000000u +
                              0x00440000u * static_cast<std::uint32_t>(random_int(1, 4)) +
                              0x00004400u * static_cast<std::uint32_t>(random_int(1, 4)) +
                              0x00000044u * static_cast<std::uint32_t>(random_int(1, 4));

  Entity ent;
  ent.color = color;
  ent.x = random_spawn();
  ent.y = random_spawn();
  ent.eid = newEid;
  ent.serverControlled = false;
  ent.targetX = 0.f;
  ent.targetY = 0.f;
  ent.size = 5.f + static_cast<float>(random_int(0, 5));
  ent.score = 0;

  entities.push_back(ent);
  return newEid;
}

static void broadcast_new_entity(socketwire_examples::ServerConnectionHub& hub, const Entity& ent)
{
  for (auto* client : hub.clients())
    if (client != nullptr && client->connection != nullptr && client->connection->isConnected())
      send_new_entity(client->connection.get(), ent);
}

static void on_join(socketwire_examples::ServerConnectionHub& hub,
                    socketwire_examples::ServerConnectionHub::Client& client)
{
  for (const Entity& ent : entities)
    send_new_entity(client.connection.get(), ent);

  const std::uint16_t newEid = create_random_entity();
  const Entity& ent = entities[newEid];
  controlledMap[newEid] = &client;

  broadcast_new_entity(hub, ent);
  send_set_controlled_entity(client.connection.get(), newEid);
}

static void on_state(const void* data, std::size_t size)
{
  std::uint16_t eid = INVALID_ENTITY;
  float x = 0.f;
  float y = 0.f;
  deserialize_entity_state(data, size, eid, x, y);

  for (Entity& e : entities)
  {
    if (e.eid == eid)
    {
      e.x = x;
      e.y = y;
      return;
    }
  }
}

static void send_to_all(socketwire_examples::ServerConnectionHub& hub,
                        void (*sendFn)(socketwire::ReliableConnection*, int),
                        int value)
{
  for (auto* client : hub.clients())
    if (client != nullptr && client->connection != nullptr && client->connection->isConnected())
      sendFn(client->connection.get(), value);
}

int main(int argc, const char** argv)
{
  auto benchOptions = socketwire_examples::benchmark::parseOptions(argc, argv, 10131);
  socketwire_examples::benchmark::MetricsCollector metrics(
    benchOptions, "entity-eater", "socketwire", "server");
  socketwire_examples::benchmark::setActiveCollector(&metrics);

  const std::uint16_t listenPort = benchOptions.enabled
    ? benchOptions.port
    : socketwire_examples::portFromArgsOrEnv(argc, argv, 1, "SOCKETWIRE_ENTITY_EATER_PORT", 10131);

  auto socket = socketwire_examples::createUdpSocket(listenPort);
  if (socket == nullptr)
    return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);

  bool createdAiEntities = false;
  constexpr int NUM_AI = 10;

  constexpr int GAME_DURATION = 60;
  int gameTimeRemaining = GAME_DURATION;
  auto lastTimeUpdate = std::chrono::steady_clock::now();
  bool gameOver = false;

  hub.setDisconnectedCallback([](auto& client)
  {
    for (auto& entry : controlledMap)
      if (entry.second == &client)
        entry.second = nullptr;
  });

  hub.setPacketCallback([&](auto& client, std::uint8_t, const void* data, std::size_t size, bool)
  {
    socketwire_examples::benchmark::recordPayloadRx(size);
    switch (get_packet_type(data, size))
    {
      case E_CLIENT_TO_SERVER_JOIN:
      {
        if (!createdAiEntities)
        {
          for (int i = 0; i < NUM_AI; ++i)
          {
            const std::uint16_t eid = create_random_entity();
            entities[eid].serverControlled = true;
            entities[eid].score = 0;
            controlledMap[eid] = nullptr;
          }
          createdAiEntities = true;
        }
        on_join(hub, client);
        break;
      }
      case E_CLIENT_TO_SERVER_STATE:
        on_state(data, size);
        break;
      case E_SERVER_TO_CLIENT_NEW_ENTITY:
      case E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY:
      case E_SERVER_TO_CLIENT_SNAPSHOT:
      case E_SERVER_TO_CLIENT_ENTITY_DEVOURED:
      case E_SERVER_TO_CLIENT_SCORE_UPDATE:
      case E_SERVER_TO_CLIENT_GAME_TIME:
      case E_SERVER_TO_CLIENT_GAME_OVER:
        break;
    }
  });

  auto lastTime = std::chrono::steady_clock::now();
  while (true)
  {
    if (benchOptions.enabled && metrics.done())
      break;
    const auto frameStart = std::chrono::steady_clock::now();
    const auto curTime = std::chrono::steady_clock::now();
    const float dt =
      std::chrono::duration_cast<std::chrono::milliseconds>(curTime - lastTime).count() * 0.001f;
    lastTime = curTime;

    const auto updateStart = std::chrono::steady_clock::now();
    hub.poll();
    hub.update();

    if (!gameOver && createdAiEntities)
    {
      const auto timeSinceUpdate =
        std::chrono::duration_cast<std::chrono::milliseconds>(curTime - lastTimeUpdate).count();
      if (timeSinceUpdate >= 1000)
      {
        --gameTimeRemaining;
        lastTimeUpdate = curTime;
        send_to_all(hub, send_game_time, gameTimeRemaining);

        if (gameTimeRemaining <= 0)
        {
          gameOver = true;
          std::uint16_t winnerEid = INVALID_ENTITY;
          int highestScore = -1;

          for (const Entity& e : entities)
          {
            if (e.score > highestScore)
            {
              highestScore = e.score;
              winnerEid = e.eid;
            }
          }

          for (auto* client : hub.clients())
            if (client != nullptr && client->connection != nullptr && client->connection->isConnected())
              send_game_over(client->connection.get(), winnerEid, highestScore);
        }
      }
    }

    for (Entity& e : entities)
    {
      if (e.serverControlled)
      {
        const float diffX = e.targetX - e.x;
        const float diffY = e.targetY - e.y;
        const float dirX = diffX > 0.f ? 1.f : -1.f;
        const float dirY = diffY > 0.f ? 1.f : -1.f;
        constexpr float SPEED = 50.f;
        e.x += dirX * SPEED * dt;
        e.y += dirY * SPEED * dt;
        if (std::fabs(diffX) < 10.f && std::fabs(diffY) < 10.f)
        {
          e.targetX = random_spawn();
          e.targetY = random_spawn();
        }
      }
    }

    bool collisionOccurred = false;
    for (std::size_t i = 0; i < entities.size() && !collisionOccurred; ++i)
    {
      for (std::size_t j = 0; j < entities.size(); ++j)
      {
        if (i == j)
          continue;

        Entity& e1 = entities[i];
        Entity& e2 = entities[j];
        if (e1.size <= 0.f || e2.size <= 0.f || e1.size > 1000.f || e2.size > 1000.f)
          continue;

        const float dx = e1.x - e2.x;
        const float dy = e1.y - e2.y;
        const float distance = std::sqrt(dx * dx + dy * dy);

        if (distance < (e1.size + e2.size) && e1.size != e2.size && distance > 0.1f)
        {
          Entity* devourer = e1.size > e2.size ? &e1 : &e2;
          Entity* devoured = e1.size > e2.size ? &e2 : &e1;
          const float sizeGain = devoured->size / 2.f;

          if (sizeGain > 0.f && sizeGain < 50.f)
          {
            constexpr float MAX_SIZE = 100.f;
            devourer->size = std::min(devourer->size + sizeGain, MAX_SIZE);
            devoured->size = 5.f + static_cast<float>(random_int(0, 4));

            if (!devoured->serverControlled)
              devoured->score = 0;

            devourer->score += static_cast<int>(sizeGain);

            for (auto* client : hub.clients())
            {
              if (client == nullptr || client->connection == nullptr || !client->connection->isConnected())
                continue;
              send_score_update(client->connection.get(), devourer->eid, devourer->score);
            }

            devoured->x = random_spawn();
            devoured->y = random_spawn();

            for (auto* client : hub.clients())
            {
              if (client == nullptr || client->connection == nullptr || !client->connection->isConnected())
                continue;
              send_entity_devoured(client->connection.get(),
                                   devoured->eid,
                                   devourer->eid,
                                   devourer->size,
                                   devoured->x,
                                   devoured->y);
            }

            collisionOccurred = true;
          }
          break;
        }
      }
    }

    for (const Entity& e : entities)
    {
      for (auto* client : hub.clients())
      {
        if (client == nullptr || client->connection == nullptr || !client->connection->isConnected())
          continue;
        if (controlledMap[e.eid] != client)
          send_snapshot(client->connection.get(), e.eid, e.x, e.y, e.size);
      }
    }
    const auto updateEnd = std::chrono::steady_clock::now();

    if (benchOptions.enabled)
    {
      const auto clients = hub.clients();
      metrics.setConnectedClients(static_cast<int>(clients.size()));
      metrics.setNetworkStats(socketwire_examples::benchmark::statsFromClients(clients));
      metrics.recordUpdateMs(static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(updateEnd - updateStart).count()) / 1000.0);
      metrics.recordFrameMs(static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - frameStart).count()) / 1000.0);
      metrics.maybeWriteSample();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  metrics.finish();
  socketwire_examples::benchmark::setActiveCollector(nullptr);
}
