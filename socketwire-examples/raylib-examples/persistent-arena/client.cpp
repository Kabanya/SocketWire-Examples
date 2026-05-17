#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "benchmark_utils.hpp"
#include "i_socket.hpp"
#include "protocol.hpp"
#include "raylib.h"
#include "reliable_connection.hpp"
#include "socket_constants.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

namespace {

enum class ScreenMode {
  kConnect,
  kConnecting,
  kPlaying,
};

struct ClientState {
  bool connected = false;
  bool welcomed = false;
  bool returnToConnect = false;
  std::uint16_t playerId = 0;
  std::string status = "not connected";
  persistent_arena::WorldSnapshot snapshot;
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
                  const persistent_arena::WorldSnapshot& snapshot) {
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
  for (const auto& resource : snapshot.resources) {
    if (std::isnan(resource.x) || std::isnan(resource.y) ||
        std::isnan(resource.radius)) {
      ++state.nanPositionCount;
    }
    if (std::isinf(resource.x) || std::isinf(resource.y) ||
        std::isinf(resource.radius)) {
      ++state.infPositionCount;
    }
  }
}

struct TextField {
  Rectangle bounds{};
  std::string text;
  bool focused = false;
  bool numeric = false;
  std::size_t maxLength = 64;
};

class ClientHandler final : public IReliableConnectionHandler {
 public:
  explicit ClientHandler(ClientState& state) : state_(state) {}

  void OnConnected() override {
    state_.connected = true;
    state_.status = "connected";
  }

  void OnDisconnected() override {
    state_.connected = false;
    state_.welcomed = false;
    state_.returnToConnect = true;
    state_.status = "disconnected";
  }

  void OnTimeout() override {
    state_.connected = false;
    state_.welcomed = false;
    state_.returnToConnect = true;
    state_.status = "connection timeout";
  }

  void OnReliableReceived(std::uint8_t, const void* data,
                          std::size_t size) override {
    socketwire_examples::benchmark::RecordPayloadRx(size);
    ++state_.appReceivedPackets;

    BitStream stream(static_cast<const std::uint8_t*>(data), size);
    persistent_arena::MessageType type{};
    if (!persistent_arena::ReadType(stream, type)) {
      ++state_.malformedPacketsAccepted;
      return;
    }

    if (type == persistent_arena::MessageType::kWelcome) {
      std::uint16_t id = 0;
      if (persistent_arena::ReadWelcome(stream, id)) {
        state_.playerId = id;
        state_.welcomed = true;
        state_.status = "playing";
      }
      return;
    }

    if (type == persistent_arena::MessageType::kSnapshot) {
      persistent_arena::WorldSnapshot snapshot;
      if (persistent_arena::ReadSnapshot(stream, snapshot)) {
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

bool IsHostChar(int key) {
  return (key >= '0' && key <= '9') || (key >= 'a' && key <= 'z') ||
         (key >= 'A' && key <= 'Z') || key == '.' || key == '-' || key == '_' ||
         key == ':';
}

bool IsPortChar(int key) { return key >= '0' && key <= '9'; }

void UpdateTextField(TextField& field) {
  if (!field.focused) return;

  int key = GetCharPressed();
  while (key > 0) {
    if (field.text.size() < field.maxLength &&
        ((field.numeric && IsPortChar(key)) ||
         (!field.numeric && IsHostChar(key)))) {
      field.text.push_back(static_cast<char>(key));
    }
    key = GetCharPressed();
  }

  if (IsKeyPressed(KEY_BACKSPACE) && !field.text.empty()) field.text.pop_back();
}

void DrawTextField(const char* label, const TextField& field) {
  const auto border =
    field.focused ? Color{30, 120, 190, 255} : Color{92, 100, 106, 255};
  DrawText(label, static_cast<int>(field.bounds.x),
           static_cast<int>(field.bounds.y - 24.0f), 18,
           Color{38, 44, 48, 255});
  DrawRectangleRec(field.bounds, Color{255, 255, 255, 255});
  DrawRectangleLinesEx(field.bounds, 2.0f, border);
  DrawText(field.text.c_str(), static_cast<int>(field.bounds.x + 12.0f),
           static_cast<int>(field.bounds.y + 12.0f), 20,
           Color{28, 32, 36, 255});
}

bool DrawButton(Rectangle bounds, const char* label) {
  const Vector2 mouse = GetMousePosition();
  const bool hovered = CheckCollisionPointRec(mouse, bounds);
  const bool pressed = hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
  DrawRectangleRec(
    bounds, hovered ? Color{37, 113, 178, 255} : Color{46, 94, 142, 255});
  DrawRectangleLinesEx(bounds, 2.0f, Color{24, 56, 86, 255});

  constexpr int font_size = 20;
  const int text_width = MeasureText(label, font_size);
  DrawText(label,
           static_cast<int>(
             bounds.x + (bounds.width - static_cast<float>(text_width)) * 0.5f),
           static_cast<int>(
             bounds.y + (bounds.height - static_cast<float>(font_size)) * 0.5f),
           font_size, WHITE);
  return pressed;
}

std::optional<SocketAddress> ParseHost(const std::string& host) {
  if (host.empty()) return std::nullopt;
  if (host == "localhost") return SocketConstants::Loopback();
  return SocketConstants::TryFromString(host.c_str());
}

Color PlayerColor(std::uint16_t id, std::uint16_t local_id) {
  if (id == local_id) return Color{20, 138, 220, 255};
  static constexpr Color kPalette[] = {
    Color{206, 74, 62, 255},
    Color{52, 150, 102, 255},
    Color{170, 112, 42, 255},
    Color{132, 92, 176, 255},
  };
  return kPalette[id % 4];
}

Vector2 LocalPlayerPosition(const ClientState& state) {
  for (const auto& player : state.snapshot.players) {
    if (player.id == state.playerId) return Vector2{player.x, player.y};
  }
  return Vector2{persistent_arena::kKWorldWidth * 0.5f,
                 persistent_arena::kKWorldHeight * 0.5f};
}

void DrawConnectScreen(TextField& host_field, TextField& port_field,
                       ClientState& state, bool& connect_requested) {
  const Vector2 mouse = GetMousePosition();
  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    host_field.focused = CheckCollisionPointRec(mouse, host_field.bounds);
    port_field.focused = CheckCollisionPointRec(mouse, port_field.bounds);
  }
  if (IsKeyPressed(KEY_TAB)) {
    host_field.focused = !host_field.focused;
    port_field.focused = !host_field.focused;
  }

  UpdateTextField(host_field);
  UpdateTextField(port_field);

  DrawText("Persistent Arena", 260, 110, 42, Color{28, 36, 42, 255});
  DrawTextField("Host", host_field);
  DrawTextField("Port", port_field);
  if (DrawButton(Rectangle{360.0f, 350.0f, 180.0f, 48.0f}, "Connect") ||
      IsKeyPressed(KEY_ENTER)) {
    connect_requested = true;
  }

  DrawText(state.status.c_str(), 300, 420, 18, Color{110, 68, 42, 255});
}

void DrawWorld(const ClientState& state, const ReliableConnection& connection) {
  DrawRectangleLines(12, 32,
                     static_cast<int>(persistent_arena::kKWorldWidth - 24.0f),
                     static_cast<int>(persistent_arena::kKWorldHeight - 44.0f),
                     Color{58, 66, 72, 255});

  for (const auto& resource : state.snapshot.resources) {
    DrawCircleV(Vector2{resource.x, resource.y}, resource.radius,
                Color{238, 190, 68, 255});
    DrawCircleLines(static_cast<int>(resource.x), static_cast<int>(resource.y),
                    resource.radius, Color{126, 96, 34, 255});
    DrawText(TextFormat("%u", resource.value),
             static_cast<int>(resource.x - 4.0f),
             static_cast<int>(resource.y - 7.0f), 13, Color{54, 42, 28, 255});
  }

  for (const auto& projectile : state.snapshot.projectiles) {
    DrawCircleV(Vector2{projectile.x, projectile.y}, 5.0f,
                Color{226, 70, 56, 255});
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
    DrawLineV(local, mouse, Color{88, 98, 104, 135});
  }

  DrawText(TextFormat("player %u  tick %u  score %u  rtt %.1fms",
                      state.playerId, state.snapshot.tick,
                      state.snapshot.globalScore, connection.GetRtt()),
           20, 8, 18, Color{30, 36, 40, 255});

  int y = 56;
  DrawText("Score", 770, 36, 18, Color{30, 36, 40, 255});
  for (const auto& player : state.snapshot.players) {
    DrawText(TextFormat("%u  %u", player.id, player.score), 770, y, 16,
             player.id == state.playerId ? Color{20, 108, 178, 255}
                                         : Color{48, 54, 58, 255});
    y += 20;
  }
}

void ResetToConnect(std::unique_ptr<ReliableConnection>& connection,
                    std::unique_ptr<ClientHandler>& handler, ClientState& state,
                    ScreenMode& mode, bool& join_sent, std::string status) {
  connection.reset();
  handler.reset();
  state = ClientState{};
  state.status = std::move(status);
  mode = ScreenMode::kConnect;
  join_sent = false;
}

socketwire_examples::benchmark::GameMetrics CollectGameMetrics(
  const ClientState& state) {
  socketwire_examples::benchmark::GameMetrics game;
  game.appSentPackets = state.appSentPackets;
  game.appReceivedPackets = state.appReceivedPackets;
  game.appReorderedPackets = state.appReorderedPackets;
  game.joinSuccessCount = state.welcomed ? 1 : 0;
  game.entityCountClient = state.snapshot.players.size() +
                           state.snapshot.projectiles.size() +
                           state.snapshot.resources.size();
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
    argc, argv, persistent_arena::kKPort);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "persistent-arena", "socketwire", "client");
  socketwire_examples::benchmark::SetActiveCollector(&metrics);

  auto socket = socketwire_examples::CreateUdpSocket(0);
  if (socket == nullptr) return 1;

  if (!bench_options.enabled) {
    InitWindow(static_cast<int>(persistent_arena::kKWorldWidth),
               static_cast<int>(persistent_arena::kKWorldHeight),
               "SocketWire persistent arena");
    SetTargetFPS(60);
  }

  TextField host_field{Rectangle{280.0f, 205.0f, 340.0f, 48.0f},
                       bench_options.host, true, false, 64};
  TextField port_field{Rectangle{280.0f, 285.0f, 160.0f, 48.0f},
                       std::to_string(bench_options.port), false, true, 5};
  const auto bench_server_address =
    socketwire_examples::ResolveAddress(bench_options.host);
  ClientState state;
  ScreenMode mode = ScreenMode::kConnect;
  std::unique_ptr<ReliableConnection> connection;
  std::unique_ptr<ClientHandler> handler;
  bool join_sent = false;
  std::uint32_t tick = 0;
  std::uint64_t bench_frame = 0;
  auto next_connect_attempt = std::chrono::steady_clock::now();
  auto next_join_attempt = std::chrono::steady_clock::now();

  auto connect_to = [&](const SocketAddress& address, std::uint16_t port,
                        const std::string& host_text) {
    ReliableConnectionConfig cfg;
    cfg.numChannels = 2;
    state = ClientState{};
    state.status = "connecting";
    handler = std::make_unique<ClientHandler>(state);
    connection = std::make_unique<ReliableConnection>(socket.get(), cfg);
    connection->SetHandler(handler.get());
    if (!connection->Connect(address, port)) {
      ResetToConnect(connection, handler, state, mode, join_sent,
                     "connect failed");
      return false;
    }

    mode = ScreenMode::kConnecting;
    join_sent = false;
    const auto now = std::chrono::steady_clock::now();
    next_connect_attempt = now + std::chrono::milliseconds(250);
    next_join_attempt = now;
    std::println("connecting to {}:{}", host_text, static_cast<unsigned>(port));
    return true;
  };

  if (bench_options.enabled) {
    connect_to(bench_server_address, bench_options.port, bench_options.host);
  }

  while (bench_options.enabled ? !metrics.Done() : !WindowShouldClose()) {
    const auto frame_start = std::chrono::steady_clock::now();
    const auto update_start = frame_start;
    const Rectangle disconnect_button{752.0f, 548.0f, 128.0f, 34.0f};

    if (connection != nullptr) connection->Tick();

    if (state.returnToConnect) {
      if (bench_options.enabled) {
        connection.reset();
        handler.reset();
        mode = ScreenMode::kConnect;
      } else {
        ResetToConnect(connection, handler, state, mode, join_sent,
                       state.status);
      }
    }

    bool connect_requested = false;
    if (mode == ScreenMode::kConnect) {
      if (bench_options.enabled &&
          std::chrono::steady_clock::now() >= next_connect_attempt) {
        connect_to(bench_server_address, bench_options.port,
                   bench_options.host);
      }
      if (!bench_options.enabled && IsKeyPressed(KEY_ENTER)) {
        connect_requested = true;
      }
    } else if (mode == ScreenMode::kConnecting && connection != nullptr) {
      const auto now = std::chrono::steady_clock::now();
      if (bench_options.enabled && !state.connected &&
          now >= next_connect_attempt) {
        connection->Connect(bench_server_address, bench_options.port);
        next_connect_attempt = now + std::chrono::milliseconds(250);
      }
      if (state.connected && now >= next_join_attempt) {
        auto join = persistent_arena::MakeJoin();
        const bool sent = connection->SendReliable(0, join);
        join_sent = sent || join_sent;
        if (sent) {
          socketwire_examples::benchmark::RecordPayloadTx(join.GetSizeBytes());
          ++state.appSentPackets;
        }
        next_join_attempt = now + std::chrono::milliseconds(500);
      }
      if (state.welcomed) mode = ScreenMode::kPlaying;
    } else if (mode == ScreenMode::kPlaying && connection != nullptr) {
      persistent_arena::InputState input;
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
      auto input_packet = persistent_arena::MakeInput(input);
      if (connection->SendUnreliable(1, input_packet)) {
        socketwire_examples::benchmark::RecordPayloadTx(
          input_packet.GetSizeBytes());
        ++state.appSentPackets;
      }

      const bool fire_requested =
        bench_options.enabled
          ? (bench_frame % 45 == 0)
          : (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
             !CheckCollisionPointRec(GetMousePosition(), disconnect_button)) ||
              IsKeyPressed(KEY_SPACE);
      if (fire_requested) {
        const Vector2 mouse =
          bench_options.enabled
            ? Vector2{persistent_arena::kKWorldWidth * 0.5f +
                        220.0f * input.axisX,
                      persistent_arena::kKWorldHeight * 0.5f +
                        180.0f * input.axisY}
            : GetMousePosition();
        persistent_arena::FireCommand fire;
        fire.tick = tick;
        fire.aimX = mouse.x;
        fire.aimY = mouse.y;
        auto fire_packet = persistent_arena::MakeFire(fire);
        if (connection->SendReliable(0, fire_packet)) {
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
      ClearBackground(Color{242, 244, 238, 255});

      if (mode == ScreenMode::kConnect) {
        DrawConnectScreen(host_field, port_field, state, connect_requested);
      } else if (mode == ScreenMode::kConnecting) {
        DrawText("Connecting", 350, 260, 34, Color{42, 60, 74, 255});
        DrawText(state.status.c_str(), 330, 306, 18, Color{98, 78, 50, 255});
      } else if (connection != nullptr) {
        DrawWorld(state, *connection);
        if (DrawButton(disconnect_button, "Disconnect")) {
          if (connection != nullptr) connection->Disconnect();
          ResetToConnect(connection, handler, state, mode, join_sent,
                         "disconnected");
        }
      }

      EndDrawing();
    } else {
      const auto update_end = std::chrono::steady_clock::now();
      metrics.SetConnectedClients(state.connected ? 1 : 0);
      if (connection != nullptr) {
        metrics.SetNetworkStats(
          socketwire_examples::benchmark::StatsFromConnection(*connection));
      } else {
        metrics.SetNetworkStats({});
      }
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

    if (!connect_requested) continue;

    const auto port = socketwire_examples::ParsePort(port_field.text.c_str());
    const auto address = ParseHost(host_field.text);
    if (!port || !address) {
      state.status = "invalid host or port";
      continue;
    }

    connect_to(*address, *port, host_field.text);
  }

  if (connection != nullptr) connection->Disconnect();
  metrics.Finish();
  socketwire_examples::benchmark::SetActiveCollector(nullptr);
  if (!bench_options.enabled) CloseWindow();
  return 0;
}
