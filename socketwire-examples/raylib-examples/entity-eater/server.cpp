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

constexpr float kMinEntitySize = 5.f;
constexpr float kMaxEntitySize = 100.f;
constexpr float kSpawnPadding = 25.f;
constexpr float kEatCooldownSeconds = 0.25f;
constexpr float kRespawnEatCooldownSeconds = 0.75f;
constexpr float kMinEatSizeDelta = 1.f;
constexpr int kFreeSpawnAttempts = 32;

struct SpawnPoint {
  float x = 0.f;
  float y = 0.f;
};

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

static float RandomInitialSize() {
  return kMinEntitySize + static_cast<float>(RandomInt(0, 5));
}

static float RandomRespawnSize() {
  return kMinEntitySize + static_cast<float>(RandomInt(0, 4));
}

static bool OverlapsExistingEntity(float x, float y, float size,
                                   const Entity* ignored = nullptr) {
  for (const Entity& entity : entities) {
    if (&entity == ignored || entity.size <= 0.f) continue;

    const float dx = x - entity.x;
    const float dy = y - entity.y;
    const float min_distance = size + entity.size + kSpawnPadding;
    if (dx * dx + dy * dy < min_distance * min_distance) return true;
  }
  return false;
}

static SpawnPoint RandomFreeSpawn(float size, const Entity* ignored = nullptr) {
  SpawnPoint fallback{RandomSpawn(), RandomSpawn()};
  for (int attempt = 0; attempt < kFreeSpawnAttempts; ++attempt) {
    const SpawnPoint spawn{RandomSpawn(), RandomSpawn()};
    fallback = spawn;
    if (!OverlapsExistingEntity(spawn.x, spawn.y, size, ignored)) {
      return spawn;
    }
  }
  return fallback;
}

static void PickAiTarget(Entity& entity) {
  entity.targetX = RandomSpawn();
  entity.targetY = RandomSpawn();
}

static void RespawnEntity(Entity& entity) {
  entity.size = RandomRespawnSize();
  const SpawnPoint spawn = RandomFreeSpawn(entity.size, &entity);
  entity.x = spawn.x;
  entity.y = spawn.y;
  entity.eatCooldownSeconds = kRespawnEatCooldownSeconds;
  if (entity.serverControlled) PickAiTarget(entity);
}

static std::uint16_t CreateRandomEntity() {
  const auto new_eid = static_cast<std::uint16_t>(entities.size());
  const std::uint32_t color =
    0xff000000u + 0x00440000u * static_cast<std::uint32_t>(RandomInt(1, 4)) +
    0x00004400u * static_cast<std::uint32_t>(RandomInt(1, 4)) +
    0x00000044u * static_cast<std::uint32_t>(RandomInt(1, 4));

  Entity ent;
  ent.color = color;
  ent.eid = new_eid;
  ent.serverControlled = false;
  ent.size = RandomInitialSize();
  const SpawnPoint spawn = RandomFreeSpawn(ent.size);
  ent.x = spawn.x;
  ent.y = spawn.y;
  PickAiTarget(ent);
  ent.score = 0;
  ent.eatCooldownSeconds = kRespawnEatCooldownSeconds;

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

static void BroadcastScoreUpdate(socketwire_examples::ServerConnectionHub& hub,
                                 std::uint16_t eid, int score) {
  for (auto* client : hub.Clients()) {
    if (client != nullptr && client->connection != nullptr &&
        client->connection->IsConnected()) {
      SendScoreUpdate(client->connection.get(), eid, score);
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
        case MessageType::kEClientToServerJoin: {
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
        case MessageType::kEClientToServerState:
          OnState(data, size);
          break;
        case MessageType::kEServerToClientNewEntity:
        case MessageType::kEServerToClientSetControlledEntity:
        case MessageType::kEServerToClientSnapshot:
        case MessageType::kEServerToClientEntityDevoured:
        case MessageType::kEServerToClientScoreUpdate:
        case MessageType::kEServerToClientGameTime:
        case MessageType::kEServerToClientGameOver:
          break;
      }
    });

  auto last_time = std::chrono::steady_clock::now();
  while (true) {
    if (bench_options.enabled && metrics.Done()) break;
    const auto frame_start = std::chrono::steady_clock::now();
    const auto cur_time = std::chrono::steady_clock::now();
    const float dt =
      static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(
                           cur_time - last_time)
                           .count()) *
      0.001f;
    last_time = cur_time;

    const auto update_start = std::chrono::steady_clock::now();
    hub.Poll();
    hub.Update();

    for (Entity& e : entities) {
      e.eatCooldownSeconds = std::max(0.f, e.eatCooldownSeconds - dt);
    }

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
        const float distance = std::sqrt(diff_x * diff_x + diff_y * diff_y);
        constexpr float speed = 50.f;
        if (distance <= 10.f) {
          PickAiTarget(e);
        } else if (distance > 0.001f) {
          const float step = std::min(speed * dt, distance);
          e.x += (diff_x / distance) * step;
          e.y += (diff_y / distance) * step;
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
        if (e1.eatCooldownSeconds > 0.f || e2.eatCooldownSeconds > 0.f) {
          continue;
        }
        if (std::fabs(e1.size - e2.size) < kMinEatSizeDelta) {
          continue;
        }

        const float dx = e1.x - e2.x;
        const float dy = e1.y - e2.y;
        const float distance = std::sqrt(dx * dx + dy * dy);

        if (distance < (e1.size + e2.size) && distance > 0.1f) {
          Entity* devourer = e1.size > e2.size ? &e1 : &e2;
          Entity* devoured = e1.size > e2.size ? &e2 : &e1;
          const float size_gain = devoured->size / 2.f;

          if (size_gain > 0.f && size_gain < 50.f) {
            devourer->size =
              std::min(devourer->size + size_gain, kMaxEntitySize);
            devourer->eatCooldownSeconds = kEatCooldownSeconds;

            RespawnEntity(*devoured);
            devoured->score = 0;

            devourer->score += static_cast<int>(size_gain);

            BroadcastScoreUpdate(hub, devourer->eid, devourer->score);
            BroadcastScoreUpdate(hub, devoured->eid, devoured->score);

            for (auto* client : hub.Clients()) {
              if (client == nullptr || client->connection == nullptr ||
                  !client->connection->IsConnected()) {
                continue;
              }
              SendEntityDevoured(client->connection.get(), devoured->eid,
                                 devourer->eid, devourer->size, devoured->size,
                                 devoured->x, devoured->y);
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
        SendSnapshot(client->connection.get(), e.eid, e.x, e.y, e.size);
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
