#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

#include "i_socket.hpp"
#include "protocol.hpp"
#include "reliable_connection.hpp"
#include "server_connection_hub.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

namespace {

using Client = socketwire_examples::ServerConnectionHub::Client;

constexpr float K_PLAYER_RADIUS = 15.0f;
constexpr float K_PROJECTILE_RADIUS = 5.0f;
constexpr float K_PLAYER_SPEED = 190.0f;
constexpr float K_PROJECTILE_SPEED = 430.0f;
constexpr std::size_t K_RESOURCE_COUNT = 12;

struct PlayerState {
  std::uint16_t id = 0;
  Client* client = nullptr;
  float x = persistent_arena::K_WORLD_WIDTH * 0.5f;
  float y = persistent_arena::K_WORLD_HEIGHT * 0.5f;
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
std::uint16_t nextPlayerId = 1;
std::uint16_t nextProjectileId = 1;
std::uint16_t nextResourceId = 1;
std::uint32_t serverTick = 0;
std::uint32_t globalScore = 0;

float clamp_axis(float value) { return std::clamp(value, -1.0f, 1.0f); }

float random_between(float min, float max) {
  std::uniform_real_distribution<float> dist(min, max);
  return dist(rng);
}

ResourceState make_resource() {
  std::uniform_int_distribution<int> valueDist(1, 4);
  const auto value = static_cast<std::uint16_t>(valueDist(rng));
  return ResourceState{
    nextResourceId++,
    random_between(40.0f, persistent_arena::K_WORLD_WIDTH - 40.0f),
    random_between(60.0f, persistent_arena::K_WORLD_HEIGHT - 40.0f),
    10.0f + static_cast<float>(value) * 2.0f,
    value,
  };
}

void ensure_resource_population() {
  while (resources.size() < K_RESOURCE_COUNT)
    resources.push_back(make_resource());
}

PlayerState& ensure_player(Client& client) {
  auto it = players.find(&client);
  if (it != players.end()) return it->second;

  const auto id = nextPlayerId++;
  PlayerState player;
  player.id = id;
  player.client = &client;
  player.x = 120.0f + static_cast<float>((id - 1) % 6) * 90.0f;
  player.y = 140.0f + static_cast<float>((id - 1) / 6) * 80.0f;
  auto inserted = players.emplace(&client, player).first;
  std::printf("player %u joined from port %u\n", id, client.port);

  auto welcome = persistent_arena::make_welcome(id);
  client.connection->SendReliable(0, welcome);
  return inserted->second;
}

PlayerState* find_player(std::uint16_t id) {
  for (auto& entry : players) {
    if (entry.second.id == id) return &entry.second;
  }
  return nullptr;
}

void handle_fire(PlayerState& player,
                 const persistent_arena::FireCommand& fire) {
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
    nextProjectileId++,
    player.id,
    player.x,
    player.y,
    dx * K_PROJECTILE_SPEED,
    dy * K_PROJECTILE_SPEED,
  });
}

void handle_packet(Client& client, const void* data, std::size_t size) {
  BitStream stream(static_cast<const std::uint8_t*>(data), size);
  persistent_arena::MessageType type{};
  if (!persistent_arena::read_type(stream, type)) return;

  if (type == persistent_arena::MessageType::Join) {
    ensure_player(client);
    return;
  }

  auto it = players.find(&client);
  if (it == players.end()) return;

  if (type == persistent_arena::MessageType::Input) {
    persistent_arena::InputState input;
    if (persistent_arena::read_input(stream, input)) {
      it->second.axisX = clamp_axis(input.axisX);
      it->second.axisY = clamp_axis(input.axisY);
    }
    return;
  }

  if (type == persistent_arena::MessageType::Fire) {
    persistent_arena::FireCommand fire;
    if (persistent_arena::read_fire(stream, fire))
      handle_fire(it->second, fire);
  }
}

void update_players(float dt) {
  for (auto& entry : players) {
    auto& player = entry.second;
    player.x = std::clamp(
      player.x + player.axisX * K_PLAYER_SPEED * dt, K_PLAYER_RADIUS + 5.0f,
      persistent_arena::K_WORLD_WIDTH - K_PLAYER_RADIUS - 5.0f);
    player.y = std::clamp(
      player.y + player.axisY * K_PLAYER_SPEED * dt, K_PLAYER_RADIUS + 25.0f,
      persistent_arena::K_WORLD_HEIGHT - K_PLAYER_RADIUS - 5.0f);
  }
}

void update_projectiles(float dt) {
  for (auto& projectile : projectiles) {
    projectile.x += projectile.vx * dt;
    projectile.y += projectile.vy * dt;
  }

  std::erase_if(projectiles, [](const ProjectileState& projectile) {
    return projectile.x < -20.0f ||
           projectile.x > persistent_arena::K_WORLD_WIDTH + 20.0f ||
           projectile.y < -20.0f ||
           projectile.y > persistent_arena::K_WORLD_HEIGHT + 20.0f;
  });
}

void resolve_projectile_hits() {
  std::vector<std::uint16_t> hitProjectiles;
  std::vector<std::uint16_t> hitResources;

  for (const auto& projectile : projectiles) {
    for (const auto& resource : resources) {
      const float dx = projectile.x - resource.x;
      const float dy = projectile.y - resource.y;
      const float hitRadius = K_PROJECTILE_RADIUS + resource.radius;
      if (dx * dx + dy * dy > hitRadius * hitRadius) continue;

      if (auto* player = find_player(projectile.ownerId))
        player->score += resource.value;
      globalScore += resource.value;
      hitProjectiles.push_back(projectile.id);
      hitResources.push_back(resource.id);
      break;
    }
  }

  if (hitProjectiles.empty()) return;

  std::erase_if(projectiles, [&](const ProjectileState& projectile) {
    return std::ranges::find(hitProjectiles, projectile.id) !=
           hitProjectiles.end();
  });
  std::erase_if(resources, [&](const ResourceState& resource) {
    return std::ranges::find(hitResources, resource.id) != hitResources.end();
  });
  ensure_resource_population();
}

void update_world(float dt) {
  ensure_resource_population();
  update_players(dt);
  update_projectiles(dt);
  resolve_projectile_hits();
}

persistent_arena::WorldSnapshot make_snapshot() {
  persistent_arena::WorldSnapshot snapshot;
  snapshot.tick = serverTick++;
  snapshot.globalScore = globalScore;

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

void broadcast_snapshot() {
  auto snapshot = persistent_arena::make_snapshot(make_snapshot());
  for (auto& entry : players) {
    auto& player = entry.second;
    if (player.client != nullptr && player.client->connection != nullptr)
      player.client->connection->SendUnsequenced(1, snapshot);
  }
}

}  // namespace

int main(int argc, const char** argv) {
  const std::uint16_t port = socketwire_examples::portFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_PERSISTENT_ARENA_PORT",
    persistent_arena::K_PORT);

  auto socket = socketwire_examples::createUdpSocket(port);
  if (socket == nullptr) return 1;

  ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  cfg.maxHandshakesPerSecond = 0;

  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  hub.setDisconnectedCallback([](auto& client) {
    auto it = players.find(&client);
    if (it != players.end()) {
      std::printf("player %u disconnected\n", it->second.id);
      players.erase(it);
    }
  });
  hub.setPacketCallback([](auto& client, std::uint8_t, const void* data,
                           std::size_t size,
                           bool) { handle_packet(client, data, size); });

  ensure_resource_population();
  std::printf("persistent-arena server listening on 0.0.0.0:%u\n",
              static_cast<unsigned>(port));

  auto lastFrame = std::chrono::steady_clock::now();
  auto lastSnapshot = lastFrame;
  auto lastStatus = lastFrame;

  while (true) {
    hub.poll();
    hub.update();

    const auto now = std::chrono::steady_clock::now();
    const auto frameMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrame)
        .count();
    if (frameMs >= 16) {
      lastFrame = now;
      update_world(static_cast<float>(frameMs) / 1000.0f);
    }

    if (now - lastSnapshot > std::chrono::milliseconds(50)) {
      lastSnapshot = now;
      broadcast_snapshot();
    }

    if (now - lastStatus > std::chrono::seconds(5)) {
      lastStatus = now;
      std::printf(
        "world tick=%u players=%zu resources=%zu projectiles=%zu score=%u\n",
        serverTick, players.size(), resources.size(), projectiles.size(),
        globalScore);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
