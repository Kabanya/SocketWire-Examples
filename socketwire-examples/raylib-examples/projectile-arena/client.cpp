#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <print>
#include <thread>
#include <utility>
#include <vector>

#include "benchmark_utils.hpp"
#include "i_socket.hpp"
#include "protocol.hpp"
#include "raylib.h"
#include "reliable_connection.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

namespace {

struct ClientState {
  bool connected = false;
  bool welcomed = false;
  std::uint16_t playerId = 0;
  projectile_arena::WorldSnapshot snapshot;
  bool hasSnapshot = false;
  std::uint32_t lastSnapshotTick = 0;
  std::vector<std::uint16_t> observedProjectileIds;
  std::uint64_t appSentPackets = 0;
  std::uint64_t appReceivedPackets = 0;
  std::uint64_t appReorderedPackets = 0;
  std::uint64_t fireCommandSent = 0;
  std::uint64_t duplicateProjectileCount = 0;
  std::uint64_t malformedPacketsAccepted = 0;
  std::uint64_t nanPositionCount = 0;
  std::uint64_t infPositionCount = 0;
};

void NoteSnapshot(ClientState& state,
                  const projectile_arena::WorldSnapshot& snapshot) {
  if (state.hasSnapshot && snapshot.tick < state.lastSnapshotTick) {
    ++state.appReorderedPackets;
  }
  state.hasSnapshot = true;
  state.lastSnapshotTick = snapshot.tick;

  std::vector<std::uint16_t> ids_in_snapshot;
  ids_in_snapshot.reserve(snapshot.projectiles.size());
  for (const auto& projectile : snapshot.projectiles) {
    if (std::ranges::find(ids_in_snapshot, projectile.id) !=
        ids_in_snapshot.end()) {
      ++state.duplicateProjectileCount;
    } else {
      ids_in_snapshot.push_back(projectile.id);
    }

    if (std::ranges::find(state.observedProjectileIds, projectile.id) ==
        state.observedProjectileIds.end()) {
      state.observedProjectileIds.push_back(projectile.id);
    }

    if (std::isnan(projectile.x) || std::isnan(projectile.y)) {
      ++state.nanPositionCount;
    }
    if (std::isinf(projectile.x) || std::isinf(projectile.y)) {
      ++state.infPositionCount;
    }
  }

  for (const auto& player : snapshot.players) {
    if (std::isnan(player.x) || std::isnan(player.y)) ++state.nanPositionCount;
    if (std::isinf(player.x) || std::isinf(player.y)) ++state.infPositionCount;
  }
}

class ClientHandler final : public IReliableConnectionHandler {
 public:
  explicit ClientHandler(ClientState& state) : state_(state) {}

  void OnConnected() override { state_.connected = true; }
  void OnDisconnected() override { state_.connected = false; }

  void OnReliableReceived(std::uint8_t, const void* data,
                          std::size_t size) override {
    socketwire_examples::benchmark::RecordPayloadRx(size);
    ++state_.appReceivedPackets;

    BitStream stream(static_cast<const std::uint8_t*>(data), size);
    projectile_arena::MessageType type{};
    if (!projectile_arena::ReadType(stream, type)) {
      ++state_.malformedPacketsAccepted;
      return;
    }

    if (type == projectile_arena::MessageType::kWelcome) {
      std::uint16_t id = 0;
      if (projectile_arena::ReadWelcome(stream, id)) {
        state_.playerId = id;
        state_.welcomed = true;
      }
      return;
    }

    if (type == projectile_arena::MessageType::kSnapshot) {
      projectile_arena::WorldSnapshot snapshot;
      if (projectile_arena::ReadSnapshot(stream, snapshot)) {
        NoteSnapshot(state_, snapshot);
        state_.snapshot = std::move(snapshot);
      } else {
        ++state_.malformedPacketsAccepted;
      }
    }
  }

  void OnUnreliableReceived(std::uint8_t channel, const void* data,
                            std::size_t size) override {
    OnReliableReceived(channel, data, size);
  }

 private:
  ClientState& state_;
};

Color PlayerColor(std::uint16_t id, std::uint16_t local_id) {
  if (id == local_id) return Color{20, 140, 220, 255};
  static constexpr Color kPalette[] = {
    Color{218, 86, 64, 255},
    Color{52, 160, 112, 255},
    Color{178, 118, 46, 255},
    Color{142, 92, 190, 255},
  };
  return kPalette[id % 4];
}

Vector2 LocalPlayerPosition(const ClientState& state) {
  for (const auto& player : state.snapshot.players) {
    if (player.id == state.playerId) return Vector2{player.x, player.y};
  }
  return Vector2{450.0f, 300.0f};
}

socketwire_examples::benchmark::GameMetrics CollectGameMetrics(
  const ClientState& state) {
  socketwire_examples::benchmark::GameMetrics game;
  game.appSentPackets = state.appSentPackets;
  game.appReceivedPackets = state.appReceivedPackets;
  game.appReorderedPackets = state.appReorderedPackets;
  game.joinSuccessCount = state.welcomed ? 1 : 0;
  game.entityCountClient = state.snapshot.players.size();
  game.projectileSpawnCountClient = state.observedProjectileIds.size();
  game.fireCommandSent = state.fireCommandSent;
  game.duplicateProjectileCount = state.duplicateProjectileCount;
  game.nanPositionCount = state.nanPositionCount;
  game.infPositionCount = state.infPositionCount;
  game.malformedPacketsAccepted = state.malformedPacketsAccepted;
  return game;
}

}  // namespace

int main(int argc, const char** argv) {
  auto bench_options = socketwire_examples::benchmark::ParseOptions(
    argc, argv, projectile_arena::kKPort);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "projectile-arena", "socketwire", "client");
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
      socket->Bind(SocketConstants::Any(), 0) != SocketError::kNone) {
    std::println("Cannot create projectile-arena client socket");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  ReliableConnection connection(socket.get(), cfg);
  ClientState state;
  ClientHandler handler(state);
  connection.SetHandler(&handler);
  const auto server_address =
    socketwire_examples::ResolveAddress(bench_options.host);
  connection.Connect(server_address, port);
  auto next_connect_attempt =
    std::chrono::steady_clock::now() + std::chrono::milliseconds(250);

  if (!bench_options.enabled) {
    InitWindow(900, 600, "SocketWire projectile arena");
    SetTargetFPS(60);
  }

  bool join_sent = false;
  std::uint32_t tick = 0;
  std::uint64_t bench_frame = 0;

  while (bench_options.enabled ? !metrics.Done() : !WindowShouldClose()) {
    const auto frame_start = std::chrono::steady_clock::now();
    const auto update_start = frame_start;

    if (!state.connected &&
        std::chrono::steady_clock::now() >= next_connect_attempt) {
      connection.Connect(server_address, port);
      next_connect_attempt =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    }
    connection.Update();

    if (state.connected && !join_sent) {
      auto join = projectile_arena::MakeJoin();
      join_sent = connection.SendReliable(0, join);
      if (join_sent) {
        socketwire_examples::benchmark::RecordPayloadTx(join.GetSizeBytes());
        ++state.appSentPackets;
      }
    }

    if (state.welcomed) {
      projectile_arena::InputState input;
      input.tick = tick;
      input.axisX = bench_options.enabled
                      ? socketwire_examples::benchmark::DeterministicAxis(
                          bench_options.seed, bench_frame, 0)
                      : ((IsKeyDown(KEY_D) ? 1.0f : 0.0f) -
                         (IsKeyDown(KEY_A) ? 1.0f : 0.0f));
      input.axisY = bench_options.enabled
                      ? socketwire_examples::benchmark::DeterministicAxis(
                          bench_options.seed, bench_frame, 1)
                      : ((IsKeyDown(KEY_S) ? 1.0f : 0.0f) -
                         (IsKeyDown(KEY_W) ? 1.0f : 0.0f));
      auto input_packet = projectile_arena::MakeInput(input);
      if (connection.SendUnreliable(1, input_packet)) {
        socketwire_examples::benchmark::RecordPayloadTx(
          input_packet.GetSizeBytes());
        ++state.appSentPackets;
      }

      const bool fire_requested =
        bench_options.enabled ? (bench_frame % 30 == 0)
                              : (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                                 IsKeyPressed(KEY_SPACE));
      if (fire_requested) {
        const Vector2 mouse = bench_options.enabled
                                ? Vector2{450.0f + 220.0f * input.axisX,
                                          300.0f + 180.0f * input.axisY}
                                : GetMousePosition();
        projectile_arena::FireCommand fire;
        fire.tick = tick;
        fire.aimX = mouse.x;
        fire.aimY = mouse.y;
        auto fire_packet = projectile_arena::MakeFire(fire);
        if (connection.SendReliable(0, fire_packet)) {
          socketwire_examples::benchmark::RecordPayloadTx(
            fire_packet.GetSizeBytes());
          ++state.appSentPackets;
          ++state.fireCommandSent;
        }
      }
    }

    ++tick;
    ++bench_frame;

    if (!bench_options.enabled) {
      BeginDrawing();
      ClearBackground(Color{245, 246, 242, 255});
      DrawRectangleLines(12, 12, 876, 576, Color{58, 66, 72, 255});

      for (const auto& projectile : state.snapshot.projectiles) {
        DrawCircleV(Vector2{projectile.x, projectile.y}, 5.0f,
                    Color{236, 182, 50, 255});
      }

      for (const auto& player : state.snapshot.players) {
        const auto color = PlayerColor(player.id, state.playerId);
        DrawCircleV(Vector2{player.x, player.y}, 15.0f, color);
        DrawText(TextFormat("%u", player.id), static_cast<int>(player.x - 4.0f),
                 static_cast<int>(player.y - 8.0f), 14, WHITE);
      }

      if (state.welcomed) {
        const Vector2 local = LocalPlayerPosition(state);
        const Vector2 mouse = GetMousePosition();
        DrawLineV(local, mouse, Color{110, 120, 124, 140});
      }

      DrawText(TextFormat("player %u  snapshot %u  rtt %.1fms", state.playerId,
                          state.snapshot.tick, connection.GetRtt()),
               20, 20, 18, Color{32, 38, 42, 255});
      DrawText(
        state.connected ? "connected" : "connecting", 20, 44, 16,
        state.connected ? Color{34, 120, 76, 255} : Color{180, 92, 45, 255});
      EndDrawing();
    } else {
      const auto update_end = std::chrono::steady_clock::now();
      metrics.SetConnectedClients(state.connected ? 1 : 0);
      metrics.SetNetworkStats(
        socketwire_examples::benchmark::StatsFromConnection(connection));
      metrics.SetGameMetrics(CollectGameMetrics(state));
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
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
  }

  connection.Disconnect();
  metrics.Finish();
  socketwire_examples::benchmark::SetActiveCollector(nullptr);
  if (!bench_options.enabled) CloseWindow();
  return 0;
}
