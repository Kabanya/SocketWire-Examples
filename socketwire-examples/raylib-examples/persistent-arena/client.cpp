#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "i_socket.hpp"
#include "protocol.hpp"
#include "raylib.h"
#include "reliable_connection.hpp"
#include "socket_constants.hpp"
#include "socketwire_example_utils.hpp"

using namespace socketwire;  // NOLINT

namespace {

enum class ScreenMode {
  Connect,
  Connecting,
  Playing,
};

struct ClientState {
  bool connected = false;
  bool welcomed = false;
  bool returnToConnect = false;
  std::uint16_t playerId = 0;
  std::string status = "not connected";
  persistent_arena::WorldSnapshot snapshot;
};

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
    BitStream stream(static_cast<const std::uint8_t*>(data), size);
    persistent_arena::MessageType type{};
    if (!persistent_arena::read_type(stream, type)) return;

    if (type == persistent_arena::MessageType::Welcome) {
      std::uint16_t id = 0;
      if (persistent_arena::read_welcome(stream, id)) {
        state_.playerId = id;
        state_.welcomed = true;
        state_.status = "playing";
      }
      return;
    }

    if (type == persistent_arena::MessageType::Snapshot) {
      persistent_arena::WorldSnapshot snapshot;
      if (persistent_arena::read_snapshot(stream, snapshot))
        state_.snapshot = std::move(snapshot);
    }
  }

  void OnUnreliableReceived(std::uint8_t channel, const void* data,
                            std::size_t size) override {
    OnReliableReceived(channel, data, size);
  }

 private:
  ClientState& state_;
};

bool is_host_char(int key) {
  return (key >= '0' && key <= '9') || (key >= 'a' && key <= 'z') ||
         (key >= 'A' && key <= 'Z') || key == '.' || key == '-' || key == '_' ||
         key == ':';
}

bool is_port_char(int key) { return key >= '0' && key <= '9'; }

void update_text_field(TextField& field) {
  if (!field.focused) return;

  int key = GetCharPressed();
  while (key > 0) {
    if (field.text.size() < field.maxLength &&
        ((field.numeric && is_port_char(key)) ||
         (!field.numeric && is_host_char(key)))) {
      field.text.push_back(static_cast<char>(key));
    }
    key = GetCharPressed();
  }

  if (IsKeyPressed(KEY_BACKSPACE) && !field.text.empty()) field.text.pop_back();
}

void draw_text_field(const char* label, const TextField& field) {
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

bool draw_button(Rectangle bounds, const char* label) {
  const Vector2 mouse = GetMousePosition();
  const bool hovered = CheckCollisionPointRec(mouse, bounds);
  const bool pressed = hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
  DrawRectangleRec(
    bounds, hovered ? Color{37, 113, 178, 255} : Color{46, 94, 142, 255});
  DrawRectangleLinesEx(bounds, 2.0f, Color{24, 56, 86, 255});

  constexpr int fontSize = 20;
  const int textWidth = MeasureText(label, fontSize);
  DrawText(label,
           static_cast<int>(
             bounds.x + (bounds.width - static_cast<float>(textWidth)) * 0.5f),
           static_cast<int>(
             bounds.y + (bounds.height - static_cast<float>(fontSize)) * 0.5f),
           fontSize, WHITE);
  return pressed;
}

std::optional<SocketAddress> parse_host(const std::string& host) {
  if (host.empty()) return std::nullopt;
  if (host == "localhost") return SocketConstants::Loopback();
  return SocketConstants::TryFromString(host.c_str());
}

Color player_color(std::uint16_t id, std::uint16_t localId) {
  if (id == localId) return Color{20, 138, 220, 255};
  static constexpr Color PALETTE[] = {
    Color{206, 74, 62, 255},
    Color{52, 150, 102, 255},
    Color{170, 112, 42, 255},
    Color{132, 92, 176, 255},
  };
  return PALETTE[id % 4];
}

Vector2 local_player_position(const ClientState& state) {
  for (const auto& player : state.snapshot.players) {
    if (player.id == state.playerId) return Vector2{player.x, player.y};
  }
  return Vector2{persistent_arena::K_WORLD_WIDTH * 0.5f,
                 persistent_arena::K_WORLD_HEIGHT * 0.5f};
}

void draw_connect_screen(TextField& hostField, TextField& portField,
                         ClientState& state, bool& connectRequested) {
  const Vector2 mouse = GetMousePosition();
  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    hostField.focused = CheckCollisionPointRec(mouse, hostField.bounds);
    portField.focused = CheckCollisionPointRec(mouse, portField.bounds);
  }
  if (IsKeyPressed(KEY_TAB)) {
    hostField.focused = !hostField.focused;
    portField.focused = !hostField.focused;
  }

  update_text_field(hostField);
  update_text_field(portField);

  DrawText("Persistent Arena", 260, 110, 42, Color{28, 36, 42, 255});
  draw_text_field("Host", hostField);
  draw_text_field("Port", portField);
  if (draw_button(Rectangle{360.0f, 350.0f, 180.0f, 48.0f}, "Connect") ||
      IsKeyPressed(KEY_ENTER))
    connectRequested = true;

  DrawText(state.status.c_str(), 300, 420, 18, Color{110, 68, 42, 255});
}

void draw_world(const ClientState& state,
                const ReliableConnection& connection) {
  DrawRectangleLines(12, 32,
                     static_cast<int>(persistent_arena::K_WORLD_WIDTH - 24.0f),
                     static_cast<int>(persistent_arena::K_WORLD_HEIGHT - 44.0f),
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

  for (const auto& projectile : state.snapshot.projectiles)
    DrawCircleV(Vector2{projectile.x, projectile.y}, 5.0f,
                Color{226, 70, 56, 255});

  for (const auto& player : state.snapshot.players) {
    const auto color = player_color(player.id, state.playerId);
    DrawCircleV(Vector2{player.x, player.y}, 15.0f, color);
    DrawText(TextFormat("%u", player.id), static_cast<int>(player.x - 4.0f),
             static_cast<int>(player.y - 8.0f), 14, WHITE);
  }

  if (state.welcomed) {
    const Vector2 local = local_player_position(state);
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

void reset_to_connect(std::unique_ptr<ReliableConnection>& connection,
                      std::unique_ptr<ClientHandler>& handler,
                      ClientState& state, ScreenMode& mode, bool& joinSent,
                      std::string status) {
  connection.reset();
  handler.reset();
  state = ClientState{};
  state.status = status;
  mode = ScreenMode::Connect;
  joinSent = false;
}

}  // namespace

int main() {
  auto socket = socketwire_examples::createUdpSocket(0);
  if (socket == nullptr) return 1;

  InitWindow(static_cast<int>(persistent_arena::K_WORLD_WIDTH),
             static_cast<int>(persistent_arena::K_WORLD_HEIGHT),
             "SocketWire persistent arena");
  SetTargetFPS(60);

  TextField hostField{Rectangle{280.0f, 205.0f, 340.0f, 48.0f}, "127.0.0.1",
                      true, false, 64};
  TextField portField{Rectangle{280.0f, 285.0f, 160.0f, 48.0f},
                      std::to_string(persistent_arena::K_PORT), false, true, 5};
  ClientState state;
  ScreenMode mode = ScreenMode::Connect;
  std::unique_ptr<ReliableConnection> connection;
  std::unique_ptr<ClientHandler> handler;
  bool joinSent = false;
  std::uint32_t tick = 0;
  auto nextJoinAttempt = std::chrono::steady_clock::now();

  while (!WindowShouldClose()) {
    const Rectangle disconnectButton{752.0f, 548.0f, 128.0f, 34.0f};

    if (connection != nullptr) connection->Tick();

    if (state.returnToConnect)
      reset_to_connect(connection, handler, state, mode, joinSent,
                       state.status);

    bool connectRequested = false;
    if (mode == ScreenMode::Connect) {
      if (IsKeyPressed(KEY_ENTER)) connectRequested = true;
    } else if (mode == ScreenMode::Connecting && connection != nullptr) {
      const auto now = std::chrono::steady_clock::now();
      if (state.connected && now >= nextJoinAttempt) {
        auto join = persistent_arena::make_join();
        joinSent = connection->SendReliable(0, join) || joinSent;
        nextJoinAttempt = now + std::chrono::milliseconds(500);
      }
      if (state.welcomed) mode = ScreenMode::Playing;
    } else if (mode == ScreenMode::Playing && connection != nullptr) {
      persistent_arena::InputState input;
      input.tick = tick;
      input.axisX =
        (IsKeyDown(KEY_D) ? 1.0f : 0.0f) - (IsKeyDown(KEY_A) ? 1.0f : 0.0f);
      input.axisY =
        (IsKeyDown(KEY_S) ? 1.0f : 0.0f) - (IsKeyDown(KEY_W) ? 1.0f : 0.0f);
      auto inputPacket = persistent_arena::make_input(input);
      connection->SendUnsequenced(1, inputPacket);

      const bool fireClicked =
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        !CheckCollisionPointRec(GetMousePosition(), disconnectButton);
      if (fireClicked || IsKeyPressed(KEY_SPACE)) {
        const Vector2 mouse = GetMousePosition();
        persistent_arena::FireCommand fire;
        fire.tick = tick;
        fire.aimX = mouse.x;
        fire.aimY = mouse.y;
        auto firePacket = persistent_arena::make_fire(fire);
        connection->SendReliable(0, firePacket);
      }
    }

    ++tick;

    BeginDrawing();
    ClearBackground(Color{242, 244, 238, 255});

    if (mode == ScreenMode::Connect) {
      draw_connect_screen(hostField, portField, state, connectRequested);
    } else if (mode == ScreenMode::Connecting) {
      DrawText("Connecting", 350, 260, 34, Color{42, 60, 74, 255});
      DrawText(state.status.c_str(), 330, 306, 18, Color{98, 78, 50, 255});
    } else if (connection != nullptr) {
      draw_world(state, *connection);
      if (draw_button(disconnectButton, "Disconnect")) {
        if (connection != nullptr) connection->Disconnect();
        reset_to_connect(connection, handler, state, mode, joinSent,
                         "disconnected");
      }
    }

    EndDrawing();

    if (!connectRequested) continue;

    const auto port = socketwire_examples::parsePort(portField.text.c_str());
    const auto address = parse_host(hostField.text);
    if (!port || !address) {
      state.status = "invalid host or port";
      continue;
    }

    ReliableConnectionConfig cfg;
    cfg.numChannels = 2;
    state = ClientState{};
    state.status = "connecting";
    handler = std::make_unique<ClientHandler>(state);
    connection = std::make_unique<ReliableConnection>(socket.get(), cfg);
    connection->SetHandler(handler.get());
    if (!connection->Connect(*address, *port)) {
      reset_to_connect(connection, handler, state, mode, joinSent,
                       "connect failed");
      continue;
    }

    mode = ScreenMode::Connecting;
    joinSent = false;
    nextJoinAttempt = std::chrono::steady_clock::now();
    std::printf("connecting to %s:%u\n", hostField.text.c_str(),
                static_cast<unsigned>(*port));
  }

  if (connection != nullptr) connection->Disconnect();
  CloseWindow();
  return 0;
}
