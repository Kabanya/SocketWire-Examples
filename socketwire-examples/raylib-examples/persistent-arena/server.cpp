#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <print>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

#include "benchmark_utils.hpp"
#include "i_socket.hpp"
#include "protocol.hpp"
#include "reliable_connection.hpp"
#include "server_connection_hub.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

namespace {

using Client = socketwire_examples::ServerConnectionHub::Client;

constexpr float kKPlayerRadius = 15.0f;
constexpr float kKProjectileRadius = 5.0f;
constexpr float kKPlayerSpeed = 190.0f;
constexpr float kKProjectileSpeed = 430.0f;
constexpr std::size_t kKResourceCount = 12;

struct PlayerState {
  std::uint16_t id = 0;
  Client* client = nullptr;
  float x = persistent_arena::kKWorldWidth * 0.5f;
  float y = persistent_arena::kKWorldHeight * 0.5f;
  float axisX = 0.0f;
  float axisY = 0.0f;
  std::uint32_t score = 0;
};

struct ProjectileState {
  std::uint16_t id = 0;
  std::uint16_t ownerId = 0;
  float x = 0.0f;
  float y = 0.0f;
  float vx = 0.0f;
  float vy = 0.0f;
};

struct ResourceState {
  std::uint16_t id = 0;
  float x = 0.0f;
  float y = 0.0f;
  float radius = 12.0f;
  std::uint16_t value = 1;
};

std::unordered_map<Client*, PlayerState> players;
std::vector<ProjectileState> projectiles;
std::vector<ResourceState> resources;
std::mt19937 rng(0x534f434b);
std::uint16_t next_player_id = 1;
std::uint16_t next_projectile_id = 1;
std::uint16_t next_resource_id = 1;
std::uint32_t server_tick = 0;
std::uint32_t global_score = 0;
std::uint64_t fire_command_accepted = 0;

float ClampAxis(float value) { return std::clamp(value, -1.0f, 1.0f); }

float RandomBetween(float min, float max) {
  std::uniform_real_distribution<float> dist(min, max);
  return dist(rng);
}

ResourceState MakeResource() {
  std::uniform_int_distribution<int> value_dist(1, 4);
  const auto value = static_cast<std::uint16_t>(value_dist(rng));
  return ResourceState{
    next_resource_id++,
    RandomBetween(40.0f, persistent_arena::kKWorldWidth - 40.0f),
    RandomBetween(60.0f, persistent_arena::kKWorldHeight - 40.0f),
    10.0f + static_cast<float>(value) * 2.0f,
    value,
  };
}

void EnsureResourcePopulation() {
  while (resources.size() < kKResourceCount) {
    resources.push_back(MakeResource());
  }
}

PlayerState& EnsurePlayer(Client& client) {
  auto it = players.find(&client);
  if (it != players.end()) return it->second;

  const auto id = next_player_id++;
  const auto row = static_cast<std::uint16_t>((id - 1) / 6);
  PlayerState player;
  player.id = id;
  player.client = &client;
  player.x = 120.0f + static_cast<float>((id - 1) % 6) * 90.0f;
  player.y = 140.0f + static_cast<float>(row) * 80.0f;
  auto inserted = players.emplace(&client, player).first;
  std::println("player {} joined from port {}", id, client.port);

  auto welcome = persistent_arena::MakeWelcome(id);
  if (client.connection->SendReliable(0, welcome)) {
    socketwire_examples::benchmark::RecordPayloadTx(welcome.GetSizeBytes());
  }
  return inserted->second;
}

PlayerState* FindPlayer(std::uint16_t id) {
  for (auto& entry : players) {
    if (entry.second.id == id) return &entry.second;
  }
  return nullptr;
}

void HandleFire(PlayerState& player,
                const persistent_arena::FireCommand& fire) {
  ++fire_command_accepted;
  float dx = fire.aimX - player.x;
  float dy = fire.aimY - player.y;
  const float length = std::sqrt(dx * dx + dy * dy);
  if (length < 0.001f) {
    dx = 1.0f;
    dy = 0.0f;
  } else {
    dx /= length;
    dy /= length;
  }

  projectiles.push_back(ProjectileState{
    next_projectile_id++,
    player.id,
    player.x,
    player.y,
    dx * kKProjectileSpeed,
    dy * kKProjectileSpeed,
  });
}

void HandlePacket(Client& client, const void* data, std::size_t size) {
  socketwire_examples::benchmark::RecordPayloadRx(size);

  BitStream stream(static_cast<const std::uint8_t*>(data), size);
  persistent_arena::MessageType type{};
  if (!persistent_arena::ReadType(stream, type)) return;

  if (type == persistent_arena::MessageType::kJoin) {
    EnsurePlayer(client);
    return;
  }

  auto it = players.find(&client);
  if (it == players.end()) return;

  if (type == persistent_arena::MessageType::kInput) {
    persistent_arena::InputState input;
    if (persistent_arena::ReadInput(stream, input)) {
      it->second.axisX = ClampAxis(input.axisX);
      it->second.axisY = ClampAxis(input.axisY);
    }
    return;
  }

  if (type == persistent_arena::MessageType::kFire) {
    persistent_arena::FireCommand fire;
    if (persistent_arena::ReadFire(stream, fire)) {
      HandleFire(it->second, fire);
    }
  }
}

void UpdatePlayers(float dt) {
  for (auto& entry : players) {
    auto& player = entry.second;
    player.x = std::clamp(
      player.x + player.axisX * kKPlayerSpeed * dt, kKPlayerRadius + 5.0f,
      persistent_arena::kKWorldWidth - kKPlayerRadius - 5.0f);
    player.y = std::clamp(
      player.y + player.axisY * kKPlayerSpeed * dt, kKPlayerRadius + 25.0f,
      persistent_arena::kKWorldHeight - kKPlayerRadius - 5.0f);
  }
}

void UpdateProjectiles(float dt) {
  for (auto& projectile : projectiles) {
    projectile.x += projectile.vx * dt;
    projectile.y += projectile.vy * dt;
  }

  std::erase_if(projectiles, [](const ProjectileState& projectile) {
    return projectile.x < -20.0f ||
           projectile.x > persistent_arena::kKWorldWidth + 20.0f ||
           projectile.y < -20.0f ||
           projectile.y > persistent_arena::kKWorldHeight + 20.0f;
  });
}

void ResolveProjectileHits() {
  std::vector<std::uint16_t> hit_projectiles;
  std::vector<std::uint16_t> hit_resources;

  for (const auto& projectile : projectiles) {
    for (const auto& resource : resources) {
      const float dx = projectile.x - resource.x;
      const float dy = projectile.y - resource.y;
      const float hit_radius = kKProjectileRadius + resource.radius;
      if (dx * dx + dy * dy > hit_radius * hit_radius) continue;

      if (auto* player = FindPlayer(projectile.ownerId)) {
        player->score += resource.value;
      }
      global_score += resource.value;
      hit_projectiles.push_back(projectile.id);
      hit_resources.push_back(resource.id);
      break;
    }
  }

  if (hit_projectiles.empty()) return;

  std::erase_if(projectiles, [&](const ProjectileState& projectile) {
    return std::ranges::find(hit_projectiles, projectile.id) !=
           hit_projectiles.end();
  });
  std::erase_if(resources, [&](const ResourceState& resource) {
    return std::ranges::find(hit_resources, resource.id) != hit_resources.end();
  });
  EnsureResourcePopulation();
}

void UpdateWorld(float dt) {
  EnsureResourcePopulation();
  UpdatePlayers(dt);
  UpdateProjectiles(dt);
  ResolveProjectileHits();
}

persistent_arena::WorldSnapshot MakeSnapshot() {
  persistent_arena::WorldSnapshot snapshot;
  snapshot.tick = server_tick++;
  snapshot.globalScore = global_score;

  snapshot.players.reserve(players.size());
  for (const auto& entry : players) {
    const auto& player = entry.second;
    snapshot.players.push_back(persistent_arena::PlayerSnapshot{
      player.id,
      player.x,
      player.y,
      player.score,
    });
  }

  snapshot.projectiles.reserve(projectiles.size());
  for (const auto& projectile : projectiles) {
    snapshot.projectiles.push_back(persistent_arena::ProjectileSnapshot{
      projectile.id,
      projectile.ownerId,
      projectile.x,
      projectile.y,
    });
  }

  snapshot.resources.reserve(resources.size());
  for (const auto& resource : resources) {
    snapshot.resources.push_back(persistent_arena::ResourceSnapshot{
      resource.id,
      resource.x,
      resource.y,
      resource.radius,
      resource.value,
    });
  }
  return snapshot;
}

void BroadcastSnapshot() {
  auto snapshot = persistent_arena::MakeSnapshot(MakeSnapshot());
  for (auto& entry : players) {
    auto& player = entry.second;
    if (player.client != nullptr && player.client->connection != nullptr) {
      if (player.client->connection->SendUnreliable(1, snapshot)) {
        socketwire_examples::benchmark::RecordPayloadTx(
          snapshot.GetSizeBytes());
      }
    }
  }
}

socketwire_examples::benchmark::GameMetrics CollectGameMetrics() {
  socketwire_examples::benchmark::GameMetrics game;
  game.entityCountServer = players.size();
  game.projectileSpawnCountServer =
    next_projectile_id > 0 ? static_cast<std::uint64_t>(next_projectile_id - 1)
                           : 0;
  game.fireCommandAccepted = fire_command_accepted;

  for (const auto& entry : players) {
    const auto& player = entry.second;
    if (std::isnan(player.x) || std::isnan(player.y)) ++game.nanPositionCount;
    if (std::isinf(player.x) || std::isinf(player.y)) ++game.infPositionCount;
  }
  for (const auto& projectile : projectiles) {
    if (std::isnan(projectile.x) || std::isnan(projectile.y)) {
      ++game.nanPositionCount;
    }
    if (std::isinf(projectile.x) || std::isinf(projectile.y)) {
      ++game.infPositionCount;
    }
  }
  for (const auto& resource : resources) {
    if (std::isnan(resource.x) || std::isnan(resource.y) ||
        std::isnan(resource.radius)) {
      ++game.nanPositionCount;
    }
    if (std::isinf(resource.x) || std::isinf(resource.y) ||
        std::isinf(resource.radius)) {
      ++game.infPositionCount;
    }
  }

  return game;
}

}  // namespace

int main(int argc, const char** argv) {
  auto bench_options = socketwire_examples::benchmark::ParseOptions(
    argc, argv, persistent_arena::kKPort);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "persistent-arena", "socketwire", "server");
  socketwire_examples::benchmark::SetActiveCollector(&metrics);

  const std::uint16_t port =
    bench_options.enabled ? bench_options.port
                          : socketwire_examples::PortFromArgsOrEnv(
                              argc, argv, 1, "SOCKETWIRE_PERSISTENT_ARENA_PORT",
                              persistent_arena::kKPort);

  auto socket = socketwire_examples::CreateUdpSocket(port);
  if (socket == nullptr) return 1;

  ReliableConnectionConfig cfg;
  cfg.numChannels = 2;

  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  hub.SetDisconnectedCallback([](auto& client) {
    auto it = players.find(&client);
    if (it != players.end()) {
      std::println("player {:d} disconnected", it->second.id);
      players.erase(it);
    }
  });
  hub.SetPacketCallback([](auto& client, std::uint8_t, const void* data,
                           std::size_t size,
                           bool) { HandlePacket(client, data, size); });

  EnsureResourcePopulation();
  std::println("persistent-arena server listening on 0.0.0.0:{}",
               static_cast<unsigned>(port));

  auto last_frame = std::chrono::steady_clock::now();
  auto last_snapshot = last_frame;
  auto last_status = last_frame;

  while (true) {
    const auto frame_start = std::chrono::steady_clock::now();
    const auto update_start = frame_start;

    hub.Poll();
    hub.Update();

    const auto now = std::chrono::steady_clock::now();
    const auto frame_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame)
        .count();
    if (frame_ms >= 16) {
      last_frame = now;
      UpdateWorld(static_cast<float>(frame_ms) / 1000.0f);
    }

    if (now - last_snapshot > std::chrono::milliseconds(50)) {
      last_snapshot = now;
      BroadcastSnapshot();
    }

    if (!bench_options.enabled && now - last_status > std::chrono::seconds(5)) {
      last_status = now;
      std::println(
        "world tick={} players={} resources={} projectiles={} score={}",
        server_tick, players.size(), resources.size(), projectiles.size(),
        global_score);
    }

    if (bench_options.enabled) {
      const auto update_end = std::chrono::steady_clock::now();
      const auto clients = hub.Clients();
      metrics.SetConnectedClients(static_cast<int>(clients.size()));
      metrics.SetNetworkStats(
        socketwire_examples::benchmark::StatsFromClients(clients));
      metrics.SetGameMetrics(CollectGameMetrics());
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
      if (metrics.Done()) break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  metrics.Finish();
  socketwire_examples::benchmark::SetActiveCollector(nullptr);
}
