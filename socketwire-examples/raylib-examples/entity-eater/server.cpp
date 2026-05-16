#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <thread>
#include <vector>

#include "benchmark_utils.hpp"
#include "entity.h"
#include "protocol.h"
#include "server_connection_hub.hpp"
#include "socketwire_example_utils.hpp"

static std::vector<Entity> entities;
static std::map<std::uint16_t,
                socketwire_examples::ServerConnectionHub::Client*>
  controlled_map;

static std::mt19937& RandomGenerator() {
  static std::random_device random_device;
  static std::mt19937 generator(random_device());
  return generator;
}

static int RandomInt(int min, int max) {
  std::uniform_int_distribution<int> distribution(min, max);
  return distribution(RandomGenerator());
}

static float RandomSpawn(const float max_size = 10.f) {
  return static_cast<float>(RandomInt(-50, 49)) * max_size;
}

static std::uint16_t CreateRandomEntity() {
  const auto new_eid = static_cast<std::uint16_t>(entities.size());
  const std::uint32_t color =
    0xff000000u + 0x00440000u * static_cast<std::uint32_t>(RandomInt(1, 4)) +
    0x00004400u * static_cast<std::uint32_t>(RandomInt(1, 4)) +
    0x00000044u * static_cast<std::uint32_t>(RandomInt(1, 4));

  Entity ent;
  ent.color = color;
  ent.x = RandomSpawn();
  ent.y = RandomSpawn();
  ent.eid = new_eid;
  ent.serverControlled = false;
  ent.targetX = 0.f;
  ent.targetY = 0.f;
  ent.size = 5.f + static_cast<float>(RandomInt(0, 5));
  ent.score = 0;

  entities.push_back(ent);
  return new_eid;
}

static void BroadcastNewEntity(socketwire_examples::ServerConnectionHub& hub,
                               const Entity& ent) {
  for (auto* client : hub.Clients()) {
    if (client != nullptr && client->connection != nullptr &&
        client->connection->IsConnected()) {
      SendNewEntity(client->connection.get(), ent);
    }
  }
}

static void OnJoin(socketwire_examples::ServerConnectionHub& hub,
                   socketwire_examples::ServerConnectionHub::Client& client) {
  for (const Entity& ent : entities) {
    SendNewEntity(client.connection.get(), ent);
  }

  const std::uint16_t new_eid = CreateRandomEntity();
  const Entity& ent = entities[new_eid];
  controlled_map[new_eid] = &client;

  BroadcastNewEntity(hub, ent);
  SendSetControlledEntity(client.connection.get(), new_eid);
}

static void OnState(const void* data, std::size_t size) {
  std::uint16_t eid = kInvalidEntity;
  float x = 0.f;
  float y = 0.f;
  DeserializeEntityState(data, size, eid, x, y);

  for (Entity& e : entities) {
    if (e.eid == eid) {
      e.x = x;
      e.y = y;
      return;
    }
  }
}

static void SendToAll(socketwire_examples::ServerConnectionHub& hub,
                      void (*send_fn)(socketwire::ReliableConnection*, int),
                      int value) {
  for (auto* client : hub.Clients()) {
    if (client != nullptr && client->connection != nullptr &&
        client->connection->IsConnected()) {
      send_fn(client->connection.get(), value);
    }
  }
}

int main(int argc, const char** argv) {
  auto bench_options =
    socketwire_examples::benchmark::ParseOptions(argc, argv, 10131);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "entity-eater", "socketwire", "server");
  socketwire_examples::benchmark::SetActiveCollector(&metrics);

  const std::uint16_t listen_port =
    bench_options.enabled
      ? bench_options.port
      : socketwire_examples::PortFromArgsOrEnv(
          argc, argv, 1, "SOCKETWIRE_ENTITY_EATER_PORT", 10131);

  auto socket = socketwire_examples::CreateUdpSocket(listen_port);
  if (socket == nullptr) return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);

  bool created_ai_entities = false;
  constexpr int num_ai = 10;

  constexpr int game_duration = 60;
  int game_time_remaining = game_duration;
  auto last_time_update = std::chrono::steady_clock::now();
  bool game_over = false;

  hub.SetDisconnectedCallback([](auto& client) {
    for (auto& entry : controlled_map) {
      if (entry.second == &client) entry.second = nullptr;
    }
  });

  hub.SetPacketCallback(
    [&](auto& client, std::uint8_t, const void* data, std::size_t size, bool) {
      socketwire_examples::benchmark::RecordPayloadRx(size);
      switch (GetPacketType(data, size)) {
        case kEClientToServerJoin: {
          if (!created_ai_entities) {
            for (int i = 0; i < num_ai; ++i) {
              const std::uint16_t eid = CreateRandomEntity();
              entities[eid].serverControlled = true;
              entities[eid].score = 0;
              controlled_map[eid] = nullptr;
            }
            created_ai_entities = true;
          }
          OnJoin(hub, client);
          break;
        }
        case kEClientToServerState:
          OnState(data, size);
          break;
        case kEServerToClientNewEntity:
        case kEServerToClientSetControlledEntity:
        case kEServerToClientSnapshot:
        case kEServerToClientEntityDevoured:
        case kEServerToClientScoreUpdate:
        case kEServerToClientGameTime:
        case kEServerToClientGameOver:
          break;
      }
    });

  auto last_time = std::chrono::steady_clock::now();
  while (true) {
    if (bench_options.enabled && metrics.Done()) break;
    const auto frame_start = std::chrono::steady_clock::now();
    const auto cur_time = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                       cur_time - last_time)
                       .count() *
                     0.001f;
    last_time = cur_time;

    const auto update_start = std::chrono::steady_clock::now();
    hub.Poll();
    hub.Update();

    if (!game_over && created_ai_entities) {
      const auto time_since_update =
        std::chrono::duration_cast<std::chrono::milliseconds>(cur_time -
                                                              last_time_update)
          .count();
      if (time_since_update >= 1000) {
        --game_time_remaining;
        last_time_update = cur_time;
        SendToAll(hub, SendGameTime, game_time_remaining);

        if (game_time_remaining <= 0) {
          game_over = true;
          std::uint16_t winner_eid = kInvalidEntity;
          int highest_score = -1;

          for (const Entity& e : entities) {
            if (e.score > highest_score) {
              highest_score = e.score;
              winner_eid = e.eid;
            }
          }

          for (auto* client : hub.Clients()) {
            if (client != nullptr && client->connection != nullptr &&
                client->connection->IsConnected()) {
              SendGameOver(client->connection.get(), winner_eid, highest_score);
            }
          }
        }
      }
    }

    for (Entity& e : entities) {
      if (e.serverControlled) {
        const float diff_x = e.targetX - e.x;
        const float diff_y = e.targetY - e.y;
        const float dir_x = diff_x > 0.f ? 1.f : -1.f;
        const float dir_y = diff_y > 0.f ? 1.f : -1.f;
        constexpr float speed = 50.f;
        e.x += dir_x * speed * dt;
        e.y += dir_y * speed * dt;
        if (std::fabs(diff_x) < 10.f && std::fabs(diff_y) < 10.f) {
          e.targetX = RandomSpawn();
          e.targetY = RandomSpawn();
        }
      }
    }

    bool collision_occurred = false;
    for (std::size_t i = 0; i < entities.size() && !collision_occurred; ++i) {
      for (std::size_t j = 0; j < entities.size(); ++j) {
        if (i == j) continue;

        Entity& e1 = entities[i];
        Entity& e2 = entities[j];
        if (e1.size <= 0.f || e2.size <= 0.f || e1.size > 1000.f ||
            e2.size > 1000.f) {
          continue;
        }

        const float dx = e1.x - e2.x;
        const float dy = e1.y - e2.y;
        const float distance = std::sqrt(dx * dx + dy * dy);

        if (distance < (e1.size + e2.size) && e1.size != e2.size &&
            distance > 0.1f) {
          Entity* devourer = e1.size > e2.size ? &e1 : &e2;
          Entity* devoured = e1.size > e2.size ? &e2 : &e1;
          const float size_gain = devoured->size / 2.f;

          if (size_gain > 0.f && size_gain < 50.f) {
            constexpr float max_size = 100.f;
            devourer->size = std::min(devourer->size + size_gain, max_size);
            devoured->size = 5.f + static_cast<float>(RandomInt(0, 4));

            if (!devoured->serverControlled) devoured->score = 0;

            devourer->score += static_cast<int>(size_gain);

            for (auto* client : hub.Clients()) {
              if (client == nullptr || client->connection == nullptr ||
                  !client->connection->IsConnected()) {
                continue;
              }
              SendScoreUpdate(client->connection.get(), devourer->eid,
                              devourer->score);
            }

            devoured->x = RandomSpawn();
            devoured->y = RandomSpawn();

            for (auto* client : hub.Clients()) {
              if (client == nullptr || client->connection == nullptr ||
                  !client->connection->IsConnected()) {
                continue;
              }
              SendEntityDevoured(client->connection.get(), devoured->eid,
                                 devourer->eid, devourer->size, devoured->x,
                                 devoured->y);
            }

            collision_occurred = true;
          }
          break;
        }
      }
    }

    for (const Entity& e : entities) {
      for (auto* client : hub.Clients()) {
        if (client == nullptr || client->connection == nullptr ||
            !client->connection->IsConnected()) {
          continue;
        }
        if (controlled_map[e.eid] != client) {
          SendSnapshot(client->connection.get(), e.eid, e.x, e.y, e.size);
        }
      }
    }
    const auto update_end = std::chrono::steady_clock::now();

    if (bench_options.enabled) {
      const auto clients = hub.Clients();
      metrics.SetConnectedClients(static_cast<int>(clients.size()));
      metrics.SetNetworkStats(
        socketwire_examples::benchmark::StatsFromClients(clients));
      metrics.RecordUpdateMs(
        static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(update_end -
                                                                update_start)
            .count()) /
        1000.0);
      metrics.RecordFrameMs(
        static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - frame_start)
            .count()) /
        1000.0);
      metrics.MaybeWriteSample();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  metrics.Finish();
  socketwire_examples::benchmark::SetActiveCollector(nullptr);
}
