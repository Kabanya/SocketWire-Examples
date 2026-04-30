#include "windows_defines.hpp" // IWYU pragma: keep

#include "raylib.h"

#include "entity.h"
#include "protocol.h"
#include "reliable_connection.hpp"
#include "socketwire_example_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <utility>
#include <vector>

static std::vector<Entity> entities;
static std::unordered_map<std::uint16_t, std::size_t> indexMap;
static std::uint16_t myEntity = invalid_entity;
static std::uint32_t totalInData = 0;
static std::uint32_t totalOutData = 0;
static std::uint32_t serverTimeMsec = 0;

struct BandwidthAccumulator
{
  std::vector<std::pair<std::uint32_t, float>> inData;
  std::vector<std::pair<std::uint32_t, float>> outData;
  float curTime = 0.f;
};

static std::uint32_t get_delta_data(const std::vector<std::pair<std::uint32_t, float>>& data)
{
  if (data.empty())
    return 0;
  return data.back().first - data.front().first;
}

static void on_new_entity_packet(const void* data, std::size_t size)
{
  Entity newEntity;
  deserialize_new_entity(data, size, newEntity);
  if (indexMap.contains(newEntity.eid))
    return;

  indexMap[newEntity.eid] = entities.size();
  entities.push_back(newEntity);
}

static void on_set_controlled_entity(const void* data, std::size_t size)
{
  deserialize_set_controlled_entity(data, size, myEntity);
}

template<typename Callable>
static void get_entity(std::uint16_t eid, Callable callable)
{
  const auto it = indexMap.find(eid);
  if (it != indexMap.end())
    callable(entities[it->second]);
}

static void on_snapshot(const void* data, std::size_t size)
{
  std::uint16_t eid = invalid_entity;
  float x = 0.f;
  float y = 0.f;
  float ori = 0.f;
  deserialize_snapshot(data, size, eid, x, y, ori);
  get_entity(eid, [&](Entity& e)
  {
    e.x = x;
    e.y = y;
    e.ori = ori;
  });
}

static void on_time(const void* data, std::size_t size)
{
  deserialize_time_msec(data, size, serverTimeMsec);
}

static void draw_ship(float shipLen,
                      float shipWidth,
                      float x,
                      float y,
                      const Vector2& fwd,
                      const Vector2& left,
                      Color color)
{
  DrawTriangle(Vector2{x + fwd.x * shipLen * 0.5f, y + fwd.y * shipLen * 0.5f},
               Vector2{x - fwd.x * shipLen * 0.5f - left.x * shipWidth * 0.5f,
                       y - fwd.y * shipLen * 0.5f - left.y * shipWidth * 0.5f},
               Vector2{x - fwd.x * shipLen * 0.5f + left.x * shipWidth * 0.5f,
                       y - fwd.y * shipLen * 0.5f + left.y * shipWidth * 0.5f},
               color);
}

static void draw_entity(const Entity& e)
{
  constexpr float shipLen = 3.f;
  constexpr float shipWidth = 2.f;
  const Vector2 fwd = Vector2{std::cos(e.ori), std::sin(e.ori)};
  const Vector2 left = Vector2{-fwd.y, fwd.x};
  const Vector3 hsv = ColorToHSV(GetColor(e.color));
  draw_ship(shipLen + 0.4f,
            shipWidth + 0.4f,
            e.x,
            e.y,
            fwd,
            left,
            ColorFromHSV(static_cast<float>(static_cast<int>(hsv.x + 120.f) % 360), 1.f, 1.f));
  draw_ship(shipLen, shipWidth, e.x, e.y, fwd, left, ColorFromHSV(hsv.x, hsv.y, hsv.z));
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
    totalInData += static_cast<std::uint32_t>(size);
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
      case E_SERVER_TO_CLIENT_TIME_MSEC:
        on_time(data, size);
        break;
      case E_CLIENT_TO_SERVER_JOIN:
      case E_CLIENT_TO_SERVER_INPUT:
        break;
    }
  }
};

static void simulate_world(socketwire::ReliableConnection& connection)
{
  if (myEntity == invalid_entity)
    return;

  const bool left = IsKeyDown(KEY_LEFT);
  const bool right = IsKeyDown(KEY_RIGHT);
  const bool up = IsKeyDown(KEY_UP);
  const bool down = IsKeyDown(KEY_DOWN);

  get_entity(myEntity, [&](Entity&)
  {
    const float thr = (up ? 1.f : 0.f) + (down ? -1.f : 0.f);
    const float steer = (left ? -1.f : 0.f) + (right ? 1.f : 0.f);
    send_entity_input(&connection, myEntity, thr, steer);
    totalOutData += static_cast<std::uint32_t>(sizeof(std::uint8_t) + sizeof(std::uint16_t) + sizeof(std::uint8_t));
  });
}

static void draw_world(const Camera2D& camera, const BandwidthAccumulator& bw)
{
  BeginDrawing();
    ClearBackground(DARKGRAY);
    BeginMode2D(camera);

      DrawRectangleLines(-worldSize, -worldSize, 2.f * worldSize, 2.f * worldSize, WHITE);

      constexpr std::size_t numGrid = 10;
      for (std::size_t y = 1; y < numGrid; ++y)
        DrawLine(-worldSize,
                 -worldSize + 2.f * worldSize * (static_cast<float>(y) / numGrid),
                 worldSize,
                 -worldSize + 2.f * worldSize * (static_cast<float>(y) / numGrid),
                 GetColor(0xffffffff));

      for (std::size_t x = 1; x < numGrid; ++x)
        DrawLine(-worldSize + 2.f * worldSize * (static_cast<float>(x) / numGrid),
                 -worldSize,
                 -worldSize + 2.f * worldSize * (static_cast<float>(x) / numGrid),
                 worldSize,
                 GetColor(0xffffffff));

      for (const Entity& e : entities)
        draw_entity(e);

    EndMode2D();
    DrawText(TextFormat("Bandwidth: in %0.2f kbit/s", get_delta_data(bw.inData) / 1024.f), 8, 8, 12, WHITE);
    DrawText(TextFormat("Bandwidth: out %0.2f kbit/s", get_delta_data(bw.outData) / 1024.f), 8, 20, 12, WHITE);
  EndDrawing();
}

static void update_camera(Camera2D& camera)
{
  if (myEntity != invalid_entity)
  {
    get_entity(myEntity, [&](Entity& e)
    {
      camera.target.x += (e.x - camera.target.x) * 0.1f;
      camera.target.y += (e.y - camera.target.y) * 0.1f;
    });
  }
  camera.zoom *= (1.f - GetMouseWheelMove() * 0.1f);
}

static void update_bandwidth(float dt, BandwidthAccumulator& accum)
{
  constexpr float windowSize = 1.f;
  accum.curTime += dt;
  accum.inData.emplace_back(totalInData, accum.curTime);
  accum.outData.emplace_back(totalOutData, accum.curTime);

  auto eraseOld = [&](std::vector<std::pair<std::uint32_t, float>>& data)
  {
    while (!data.empty() && data.front().second < accum.curTime - windowSize)
      data.erase(data.begin());
  };
  eraseOld(accum.inData);
  eraseOld(accum.outData);
}

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

  InitWindow(width, height, "Ship Swarm");

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
  BandwidthAccumulator bandwidthAccumulator;
  while (!WindowShouldClose())
  {
    const float dt = GetFrameTime();
    connection.tick();

    if (handler.connected && !sentJoin)
    {
      send_join(&connection);
      totalOutData += 1;
      sentJoin = true;
    }

    update_bandwidth(dt, bandwidthAccumulator);
    simulate_world(connection);
    update_camera(camera);
    draw_world(camera, bandwidthAccumulator);
  }

  connection.disconnect();
  CloseWindow();
  return 0;
}
