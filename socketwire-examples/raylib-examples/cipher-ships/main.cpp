#include "windows_defines.hpp" // IWYU pragma: keep

#include "raylib.h"

#include "entity.h"
#include "protocol.h"
#include "reliable_connection.hpp"
#include "socketwire_example_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static std::vector<Entity> entities;
static std::uint16_t myEntity = invalid_entity;

static void on_new_entity_packet(const void* data, std::size_t size)
{
  Entity newEntity;
  deserialize_new_entity(data, size, newEntity);
  for (const Entity& e : entities)
    if (e.eid == newEntity.eid)
      return;
  entities.push_back(newEntity);
}

static void on_set_controlled_entity(const void* data, std::size_t size)
{
  deserialize_set_controlled_entity(data, size, myEntity);
}

static void on_snapshot(const void* data, std::size_t size)
{
  std::uint16_t eid = invalid_entity;
  float x = 0.f;
  float y = 0.f;
  float ori = 0.f;
  deserialize_snapshot(data, size, eid, x, y, ori);

  for (Entity& e : entities)
  {
    if (e.eid == eid)
    {
      e.x = x;
      e.y = y;
      e.ori = ori;
      return;
    }
  }
}

static void on_key(const void* data, std::size_t size)
{
  deserialize_and_set_key(data, size);
}

class ClientHandler final : public socketwire::IReliableConnectionHandler
{
public:
  void onConnected() override { connected = true; }
  void onDisconnected() override { connected = false; }

  void onReliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
  {
    processPacket(channel, data, size);
  }

  void onUnreliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
  {
    processPacket(channel, data, size);
  }

  bool connected = false;

private:
  static void processPacket([[maybe_unused]] std::uint8_t channel, const void* data, std::size_t size)
  {
    switch (get_packet_type(data, size))
    {
      case E_SERVER_TO_CLIENT_NEW_ENTITY:
        on_new_entity_packet(data, size);
        break;
      case E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY:
        on_set_controlled_entity(data, size);
        break;
      case E_SERVER_TO_CLIENT_SNAPSHOT:
        on_snapshot(data, size);
        break;
      case E_SERVER_TO_CLIENT_KEY:
        on_key(data, size);
        break;
      case E_CLIENT_TO_SERVER_JOIN:
      case E_CLIENT_TO_SERVER_INPUT:
        break;
    }
  }
};

int main()
{
  auto socket = socketwire_examples::createUdpSocket(0);
  if (socket == nullptr)
    return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire::ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.setHandler(&handler);
  connection.connect(socketwire::SocketConstants::loopback(), 10131);

  int width = 600;
  int height = 600;

  InitWindow(width, height, "Cipher Ships");

  const int scrWidth = GetMonitorWidth(0);
  const int scrHeight = GetMonitorHeight(0);
  if (scrWidth < width || scrHeight < height)
  {
    width = std::min(scrWidth, width);
    height = std::min(scrHeight - 150, height);
    SetWindowSize(width, height);
  }

  Camera2D camera = {{0.f, 0.f}, {0.f, 0.f}, 0.f, 1.f};
  camera.offset = Vector2{width * 0.5f, height * 0.5f};
  camera.zoom = 10.f;

  SetTargetFPS(60);

  bool sentJoin = false;
  while (!WindowShouldClose())
  {
    connection.tick();

    if (handler.connected && !sentJoin)
    {
      send_join(&connection);
      sentJoin = true;
    }

    if (myEntity != invalid_entity)
    {
      const bool left = IsKeyDown(KEY_LEFT);
      const bool right = IsKeyDown(KEY_RIGHT);
      const bool up = IsKeyDown(KEY_UP);
      const bool down = IsKeyDown(KEY_DOWN);

      for (Entity& e : entities)
      {
        if (e.eid == myEntity)
        {
          const float thr = (up ? 1.f : 0.f) + (down ? -1.f : 0.f);
          const float steer = (left ? -1.f : 0.f) + (right ? 1.f : 0.f);
          send_entity_input(&connection, myEntity, thr, steer);
        }
      }
    }

    BeginDrawing();
      ClearBackground(GRAY);
      BeginMode2D(camera);
        DrawRectangleLines(-16, -8, 32, 16, GetColor(0xff00ffff));
        for (const Entity& e : entities)
        {
          const Rectangle rect = {e.x, e.y, 3.f, 1.f};
          DrawRectanglePro(rect, {0.f, 0.5f}, e.ori * 180.f / PI, GetColor(e.color));
        }
      EndMode2D();
    EndDrawing();
  }

  connection.disconnect();
  CloseWindow();
  return 0;
}
