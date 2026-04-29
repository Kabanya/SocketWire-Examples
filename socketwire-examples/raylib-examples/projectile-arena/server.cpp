#include "protocol.hpp"
#include "server_connection_hub.hpp"

#include "i_socket.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace socketwire; // NOLINT

namespace
{

using Client = socketwire_examples::ServerConnectionHub::Client;

struct PlayerState
{
  std::uint16_t id = 0;
  Client* client = nullptr;
  float x = 450.0f;
  float y = 300.0f;
  float axisX = 0.0f;
  float axisY = 0.0f;
};

struct ProjectileState
{
  std::uint16_t id = 0;
  std::uint16_t ownerId = 0;
  float x = 0.0f;
  float y = 0.0f;
  float vx = 0.0f;
  float vy = 0.0f;
};

std::unordered_map<Client*, PlayerState> players;
std::vector<ProjectileState> projectiles;
std::uint16_t nextPlayerId = 1;
std::uint16_t nextProjectileId = 1;
std::uint32_t serverTick = 0;

float clamp_axis(float value)
{
  return std::clamp(value, -1.0f, 1.0f);
}

PlayerState& ensure_player(Client& client)
{
  auto it = players.find(&client);
  if (it != players.end())
    return it->second;

  const auto id = nextPlayerId++;
  PlayerState player;
  player.id = id;
  player.client = &client;
  player.x = 120.0f + static_cast<float>((id - 1) % 6) * 90.0f;
  player.y = 160.0f + static_cast<float>((id - 1) / 6) * 90.0f;
  auto inserted = players.emplace(&client, player).first;
  std::printf("player %u joined from port %u\n", id, client.port);

  auto welcome = projectile_arena::make_welcome(id);
  client.connection->sendReliable(0, welcome);
  return inserted->second;
}

void handle_fire(PlayerState& player, const projectile_arena::FireCommand& fire)
{
  float dx = fire.aimX - player.x;
  float dy = fire.aimY - player.y;
  const float length = std::sqrt(dx * dx + dy * dy);
  if (length < 0.001f)
  {
    dx = 1.0f;
    dy = 0.0f;
  }
  else
  {
    dx /= length;
    dy /= length;
  }

  projectiles.push_back(ProjectileState{
    nextProjectileId++,
    player.id,
    player.x,
    player.y,
    dx * 420.0f,
    dy * 420.0f,
  });
  std::printf("player %u fired at %.1f %.1f\n", player.id, fire.aimX, fire.aimY);
}

void handle_packet(Client& client, const void* data, std::size_t size)
{
  BitStream stream(static_cast<const std::uint8_t*>(data), size);
  projectile_arena::MessageType type{};
  if (!projectile_arena::read_type(stream, type))
    return;

  if (type == projectile_arena::MessageType::Join)
  {
    ensure_player(client);
    return;
  }

  auto it = players.find(&client);
  if (it == players.end())
    return;

  if (type == projectile_arena::MessageType::Input)
  {
    projectile_arena::InputState input;
    if (projectile_arena::read_input(stream, input))
    {
      it->second.axisX = clamp_axis(input.axisX);
      it->second.axisY = clamp_axis(input.axisY);
    }
    return;
  }

  if (type == projectile_arena::MessageType::Fire)
  {
    projectile_arena::FireCommand fire;
    if (projectile_arena::read_fire(stream, fire))
      handle_fire(it->second, fire);
  }
}

void update_world(float dt)
{
  for (auto& entry : players)
  {
    auto& player = entry.second;
    player.x = std::clamp(player.x + player.axisX * 180.0f * dt, 24.0f, 876.0f);
    player.y = std::clamp(player.y + player.axisY * 180.0f * dt, 24.0f, 576.0f);
  }

  for (auto& projectile : projectiles)
  {
    projectile.x += projectile.vx * dt;
    projectile.y += projectile.vy * dt;
  }

  std::erase_if(projectiles, [](const ProjectileState& projectile)
  {
    return projectile.x < -20.0f || projectile.x > 920.0f ||
           projectile.y < -20.0f || projectile.y > 620.0f;
  });
}

projectile_arena::WorldSnapshot make_snapshot()
{
  projectile_arena::WorldSnapshot snapshot;
  snapshot.tick = serverTick++;
  snapshot.players.reserve(players.size());
  for (const auto& entry : players)
  {
    const auto& player = entry.second;
    snapshot.players.push_back(projectile_arena::PlayerSnapshot{player.id, player.x, player.y});
  }

  snapshot.projectiles.reserve(projectiles.size());
  for (const auto& projectile : projectiles)
  {
    snapshot.projectiles.push_back(projectile_arena::ProjectileSnapshot{
      projectile.id,
      projectile.ownerId,
      projectile.x,
      projectile.y,
    });
  }
  return snapshot;
}

void broadcast_snapshot()
{
  auto snapshot = projectile_arena::make_snapshot(make_snapshot());
  for (auto& entry : players)
  {
    auto& player = entry.second;
    player.client->connection->sendUnsequenced(1, snapshot);
  }
}

} // namespace

int main()
{
  initialize_sockets();
  auto* factory = SocketFactoryRegistry::getFactory();
  if (factory == nullptr)
  {
    std::printf("Cannot init SocketWire\n");
    return 1;
  }

  auto socket = factory->createUDPSocket(SocketConfig{});
  if (socket == nullptr || socket->bind(SocketConstants::any(), projectile_arena::kPort) != SocketError::None)
  {
    std::printf("Cannot bind projectile-arena server\n");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  hub.setDisconnectedCallback([](auto& client)
  {
    auto it = players.find(&client);
    if (it != players.end())
    {
      std::printf("player %u disconnected\n", it->second.id);
      players.erase(it);
    }
  });
  hub.setPacketCallback([](auto& client, std::uint8_t, const void* data, std::size_t size, bool)
  {
    handle_packet(client, data, size);
  });

  std::printf("projectile-arena server listening on port %u\n", projectile_arena::kPort);
  auto lastFrame = std::chrono::steady_clock::now();
  auto lastSnapshot = lastFrame;

  while (true)
  {
    hub.poll();
    hub.update();

    const auto now = std::chrono::steady_clock::now();
    const auto frameMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrame).count();
    if (frameMs >= 16)
    {
      lastFrame = now;
      update_world(static_cast<float>(frameMs) / 1000.0f);
    }

    if (now - lastSnapshot > std::chrono::milliseconds(50))
    {
      lastSnapshot = now;
      broadcast_snapshot();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
