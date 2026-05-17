#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <print>
#include <thread>
#include <unordered_map>
#include <vector>

#include "benchmark_utils.hpp"
#include "i_socket.hpp"
#include "protocol.hpp"
#include "server_connection_hub.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

namespace {

using Client = socketwire_examples::ServerConnectionHub::Client;

struct PlayerState {
  std::uint16_t id = 0;
  Client* client = nullptr;
  float x = 450.0f;
  float y = 300.0f;
  float axisX = 0.0f;
  float axisY = 0.0f;
};

struct ProjectileState {
  std::uint16_t id = 0;
  std::uint16_t ownerId = 0;
  float x = 0.0f;
  float y = 0.0f;
  float vx = 0.0f;
  float vy = 0.0f;
};

std::unordered_map<Client*, PlayerState> players;
std::vector<ProjectileState> projectiles;
std::uint16_t next_player_id = 1;
std::uint16_t next_projectile_id = 1;
std::uint32_t server_tick = 0;
std::uint64_t fire_command_accepted = 0;

float ClampAxis(float value) { return std::clamp(value, -1.0f, 1.0f); }

PlayerState& EnsurePlayer(Client& client) {
  auto it = players.find(&client);
  if (it != players.end()) return it->second;

  const auto id = next_player_id++;
  PlayerState player;
  player.id = id;
  player.client = &client;
  player.x = 120.0f + static_cast<float>((id - 1) % 6) * 90.0f;
  player.y = 160.0f + (static_cast<float>(id - 1) / 6.0f) * 90.0f;
  auto inserted = players.emplace(&client, player).first;
  std::println("player {} joined from port {}", id, client.port);

  auto welcome = projectile_arena::MakeWelcome(id);
  if (client.connection->SendReliable(0, welcome)) {
    socketwire_examples::benchmark::RecordPayloadTx(welcome.GetSizeBytes());
  }
  return inserted->second;
}

void HandleFire(PlayerState& player,
                const projectile_arena::FireCommand& fire) {
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
    dx * 420.0f,
    dy * 420.0f,
  });
  std::println("player {} fired at {:.1f} {:.1f}", player.id, fire.aimX,
               fire.aimY);
}

void HandlePacket(Client& client, const void* data, std::size_t size) {
  socketwire_examples::benchmark::RecordPayloadRx(size);

  BitStream stream(static_cast<const std::uint8_t*>(data), size);
  projectile_arena::MessageType type{};
  if (!projectile_arena::ReadType(stream, type)) return;

  if (type == projectile_arena::MessageType::kJoin) {
    EnsurePlayer(client);
    return;
  }

  auto it = players.find(&client);
  if (it == players.end()) return;

  if (type == projectile_arena::MessageType::kInput) {
    projectile_arena::InputState input;
    if (projectile_arena::ReadInput(stream, input)) {
      it->second.axisX = ClampAxis(input.axisX);
      it->second.axisY = ClampAxis(input.axisY);
    }
    return;
  }

  if (type == projectile_arena::MessageType::kFire) {
    projectile_arena::FireCommand fire;
    if (projectile_arena::ReadFire(stream, fire)) {
      HandleFire(it->second, fire);
    }
  }
}

void UpdateWorld(float dt) {
  for (auto& entry : players) {
    auto& player = entry.second;
    player.x = std::clamp(player.x + player.axisX * 180.0f * dt, 24.0f, 876.0f);
    player.y = std::clamp(player.y + player.axisY * 180.0f * dt, 24.0f, 576.0f);
  }

  for (auto& projectile : projectiles) {
    projectile.x += projectile.vx * dt;
    projectile.y += projectile.vy * dt;
  }

  std::erase_if(projectiles, [](const ProjectileState& projectile) {
    return projectile.x < -20.0f || projectile.x > 920.0f ||
           projectile.y < -20.0f || projectile.y > 620.0f;
  });
}

projectile_arena::WorldSnapshot MakeSnapshot() {
  projectile_arena::WorldSnapshot snapshot;
  snapshot.tick = server_tick++;
  snapshot.players.reserve(players.size());
  for (const auto& entry : players) {
    const auto& player = entry.second;
    snapshot.players.push_back(
      projectile_arena::PlayerSnapshot{player.id, player.x, player.y});
  }

  snapshot.projectiles.reserve(projectiles.size());
  for (const auto& projectile : projectiles) {
    snapshot.projectiles.push_back(projectile_arena::ProjectileSnapshot{
      projectile.id,
      projectile.ownerId,
      projectile.x,
      projectile.y,
    });
  }
  return snapshot;
}

void BroadcastSnapshot() {
  auto snapshot = projectile_arena::MakeSnapshot(MakeSnapshot());
  for (auto& entry : players) {
    auto& player = entry.second;
    if (player.client->connection->SendUnreliable(1, snapshot)) {
      socketwire_examples::benchmark::RecordPayloadTx(snapshot.GetSizeBytes());
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

  return game;
}

}  // namespace

int main(int argc, const char** argv) {
  auto bench_options = socketwire_examples::benchmark::ParseOptions(
    argc, argv, projectile_arena::kKPort);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "projectile-arena", "socketwire", "server");
  socketwire_examples::benchmark::SetActiveCollector(&metrics);

  const std::uint16_t port =
    bench_options.enabled ? bench_options.port
                          : socketwire_examples::PortFromArgsOrEnv(
                              argc, argv, 1, "SOCKETWIRE_PROJECTILE_ARENA_PORT",
                              projectile_arena::kKPort);

  InitializeSockets();
  auto* factory = SocketFactoryRegistry::GetFactory();
  if (factory == nullptr) {
    std::println("Cannot init SocketWire");
    return 1;
  }

  auto socket = factory->CreateUdpSocket(SocketConfig{});
  if (socket == nullptr ||
      socket->Bind(SocketConstants::Any(), port) != SocketError::kNone) {
    std::println("Cannot bind projectile-arena server");
    return 1;
  }

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

  std::println("projectile-arena server listening on port {}",
               static_cast<unsigned>(port));
  auto last_frame = std::chrono::steady_clock::now();
  auto last_snapshot = last_frame;

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
