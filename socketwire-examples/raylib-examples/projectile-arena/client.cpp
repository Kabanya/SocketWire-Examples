#include "protocol.hpp"

#include "i_socket.hpp"
#include "raylib.h"
#include "reliable_connection.hpp"
#include "socket_constants.hpp"
#include "socket_init.hpp"
#include "socketwire_example_utils.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <utility>

using namespace socketwire; // NOLINT

namespace
{

struct ClientState
{
  bool connected = false;
  bool welcomed = false;
  std::uint16_t playerId = 0;
  projectile_arena::WorldSnapshot snapshot;
};

class ClientHandler final : public IReliableConnectionHandler
{
public:
  explicit ClientHandler(ClientState& state) : state_(state) {}

  void onConnected() override { state_.connected = true; }
  void onDisconnected() override { state_.connected = false; }

  void onReliableReceived(std::uint8_t, const void* data, std::size_t size) override
  {
    BitStream stream(static_cast<const std::uint8_t*>(data), size);
    projectile_arena::MessageType type{};
    if (!projectile_arena::read_type(stream, type))
      return;

    if (type == projectile_arena::MessageType::Welcome)
    {
      std::uint16_t id = 0;
      if (projectile_arena::read_welcome(stream, id))
      {
        state_.playerId = id;
        state_.welcomed = true;
      }
      return;
    }

    if (type == projectile_arena::MessageType::Snapshot)
    {
      projectile_arena::WorldSnapshot snapshot;
      if (projectile_arena::read_snapshot(stream, snapshot))
        state_.snapshot = std::move(snapshot);
    }
  }

  void onUnreliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
  {
    onReliableReceived(channel, data, size);
  }

private:
  ClientState& state_;
};

Color player_color(std::uint16_t id, std::uint16_t local_id)
{
  if (id == local_id)
    return Color{20, 140, 220, 255};
  static constexpr Color PALETTE[] = {
    Color{218, 86, 64, 255},
    Color{52, 160, 112, 255},
    Color{178, 118, 46, 255},
    Color{142, 92, 190, 255},
  };
  return PALETTE[id % 4];
}

Vector2 local_player_position(const ClientState& state)
{
  for (const auto& player : state.snapshot.players)
  {
    if (player.id == state.playerId)
      return Vector2{player.x, player.y};
  }
  return Vector2{450.0f, 300.0f};
}

} // namespace

int main(int argc, const char** argv)
{
  const std::uint16_t port = socketwire_examples::portFromArgsOrEnv(
    argc, argv, 1, "SOCKETWIRE_PROJECTILE_ARENA_PORT", projectile_arena::K_PORT);

  initialize_sockets();
  auto* factory = SocketFactoryRegistry::getFactory();
  if (factory == nullptr)
  {
    std::printf("Cannot init SocketWire\n");
    return 1;
  }

  auto socket = factory->createUDPSocket(SocketConfig{});
  if (socket == nullptr || socket->bind(SocketConstants::any(), 0) != SocketError::None)
  {
    std::printf("Cannot create projectile-arena client socket\n");
    return 1;
  }

  ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  ReliableConnection connection(socket.get(), cfg);
  ClientState state;
  ClientHandler handler(state);
  connection.setHandler(&handler);
  connection.connect(SocketConstants::loopback(), port);

  InitWindow(900, 600, "SocketWire projectile arena");
  SetTargetFPS(60);

  bool joinSent = false;
  std::uint32_t tick = 0;

  while (!WindowShouldClose())
  {
    connection.tick();

    if (state.connected && !joinSent)
    {
      auto join = projectile_arena::make_join();
      joinSent = connection.sendReliable(0, join);
    }

    if (state.welcomed)
    {
      projectile_arena::InputState input;
      input.tick = tick;
      input.axisX = (IsKeyDown(KEY_D) ? 1.0f : 0.0f) - (IsKeyDown(KEY_A) ? 1.0f : 0.0f);
      input.axisY = (IsKeyDown(KEY_S) ? 1.0f : 0.0f) - (IsKeyDown(KEY_W) ? 1.0f : 0.0f);
      auto inputPacket = projectile_arena::make_input(input);
      connection.sendUnreliable(1, inputPacket);

      if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_SPACE))
      {
        const Vector2 mouse = GetMousePosition();
        projectile_arena::FireCommand fire;
        fire.tick = tick;
        fire.aimX = mouse.x;
        fire.aimY = mouse.y;
        auto firePacket = projectile_arena::make_fire(fire);
        connection.sendReliable(0, firePacket);
      }
    }

    ++tick;

    BeginDrawing();
    ClearBackground(Color{245, 246, 242, 255});
    DrawRectangleLines(12, 12, 876, 576, Color{58, 66, 72, 255});

    for (const auto& projectile : state.snapshot.projectiles)
      DrawCircleV(Vector2{projectile.x, projectile.y}, 5.0f, Color{236, 182, 50, 255});

    for (const auto& player : state.snapshot.players)
    {
      const auto color = player_color(player.id, state.playerId);
      DrawCircleV(Vector2{player.x, player.y}, 15.0f, color);
      DrawText(TextFormat("%u", player.id), static_cast<int>(player.x - 4.0f), static_cast<int>(player.y - 8.0f), 14, WHITE);
    }

    if (state.welcomed)
    {
      const Vector2 local = local_player_position(state);
      const Vector2 mouse = GetMousePosition();
      DrawLineV(local, mouse, Color{110, 120, 124, 140});
    }

    DrawText(TextFormat("player %u  snapshot %u  rtt %.1fms",
                        state.playerId,
                        state.snapshot.tick,
                        connection.getRTT()),
             20,
             20,
             18,
             Color{32, 38, 42, 255});
    DrawText(state.connected ? "connected" : "connecting",
             20,
             44,
             16,
             state.connected ? Color{34, 120, 76, 255} : Color{180, 92, 45, 255});
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
