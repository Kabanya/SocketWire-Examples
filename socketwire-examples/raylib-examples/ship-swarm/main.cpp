#include "windows_defines.hpp" // IWYU pragma: keep

#include "raylib.h"

#include "entity.h"
#include "protocol.h"
#include "benchmark_utils.hpp"
#include "reliable_connection.hpp"
#include "socketwire_example_utils.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

static std::vector<Entity> entities;
static std::unordered_map<std::uint16_t, std::size_t> indexMap;
static std::uint16_t myEntity = INVALID_ENTITY;
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
  std::uint16_t eid = INVALID_ENTITY;
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

static void draw_ship(float ship_len,
                      float ship_width,
                      float x,
                      float y,
                      const Vector2& fwd,
                      const Vector2& left,
                      Color color)
{
  DrawTriangle(Vector2{x + fwd.x * ship_len * 0.5f, y + fwd.y * ship_len * 0.5f},
               Vector2{x - fwd.x * ship_len * 0.5f - left.x * ship_width * 0.5f,
                       y - fwd.y * ship_len * 0.5f - left.y * ship_width * 0.5f},
               Vector2{x - fwd.x * ship_len * 0.5f + left.x * ship_width * 0.5f,
                       y - fwd.y * ship_len * 0.5f + left.y * ship_width * 0.5f},
               color);
}

static void draw_entity(const Entity& e)
{
  constexpr float SHIP_LEN = 3.f;
  constexpr float SHIP_WIDTH = 2.f;
  const Vector2 fwd = Vector2{std::cos(e.ori), std::sin(e.ori)};
  const Vector2 left = Vector2{-fwd.y, fwd.x};
  const Vector3 hsv = ColorToHSV(GetColor(e.color));
  draw_ship(SHIP_LEN + 0.4f,
            SHIP_WIDTH + 0.4f,
            e.x,
            e.y,
            fwd,
            left,
            ColorFromHSV(static_cast<float>(static_cast<int>(hsv.x + 120.f) % 360), 1.f, 1.f));
  draw_ship(SHIP_LEN, SHIP_WIDTH, e.x, e.y, fwd, left, ColorFromHSV(hsv.x, hsv.y, hsv.z));
}

class ClientHandler final : public socketwire::IReliableConnectionHandler
{
public:
  void OnConnected() override { connected = true; }
  void OnDisconnected() override { connected = false; }

  void OnReliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
  {
    socketwire_examples::benchmark::recordPayloadRx(size);
    processPacket(channel, data, size);
  }

  void OnUnreliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
  {
    socketwire_examples::benchmark::recordPayloadRx(size);
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

static void simulate_world(socketwire::ReliableConnection& connection,
                           bool benchMode,
                           const socketwire_examples::benchmark::Options& benchOptions,
                           std::uint64_t benchFrame)
{
  if (myEntity == INVALID_ENTITY)
    return;

  get_entity(myEntity, [&](Entity&)
  {
    const float thr = benchMode
      ? socketwire_examples::benchmark::deterministicAxis(benchOptions.seed, benchFrame, 0)
      : ((IsKeyDown(KEY_UP) ? 1.f : 0.f) + (IsKeyDown(KEY_DOWN) ? -1.f : 0.f));
    const float steer = benchMode
      ? socketwire_examples::benchmark::deterministicAxis(benchOptions.seed, benchFrame, 1)
      : ((IsKeyDown(KEY_LEFT) ? -1.f : 0.f) + (IsKeyDown(KEY_RIGHT) ? 1.f : 0.f));
    send_entity_input(&connection, myEntity, thr, steer);
    totalOutData += static_cast<std::uint32_t>(sizeof(std::uint8_t) + sizeof(std::uint16_t) + sizeof(std::uint8_t));
  });
}

static void draw_world(const Camera2D& camera, const BandwidthAccumulator& bw)
{
  BeginDrawing();
    ClearBackground(DARKGRAY);
    BeginMode2D(camera);

      DrawRectangleLines(-WORLD_SIZE, -WORLD_SIZE, 2.f * WORLD_SIZE, 2.f * WORLD_SIZE, WHITE);

      constexpr std::size_t NUM_GRID = 10;
      for (std::size_t y = 1; y < NUM_GRID; ++y)
        DrawLine(-WORLD_SIZE,
                 -WORLD_SIZE + 2.f * WORLD_SIZE * (static_cast<float>(y) / NUM_GRID),
                 WORLD_SIZE,
                 -WORLD_SIZE + 2.f * WORLD_SIZE * (static_cast<float>(y) / NUM_GRID),
                 GetColor(0xffffffff));

      for (std::size_t x = 1; x < NUM_GRID; ++x)
        DrawLine(-WORLD_SIZE + 2.f * WORLD_SIZE * (static_cast<float>(x) / NUM_GRID),
                 -WORLD_SIZE,
                 -WORLD_SIZE + 2.f * WORLD_SIZE * (static_cast<float>(x) / NUM_GRID),
                 WORLD_SIZE,
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
  if (myEntity != INVALID_ENTITY)
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
  constexpr float WINDOW_SIZE = 1.f;
  accum.curTime += dt;
  accum.inData.emplace_back(totalInData, accum.curTime);
  accum.outData.emplace_back(totalOutData, accum.curTime);

  auto eraseOld = [&](std::vector<std::pair<std::uint32_t, float>>& data)
  {
    while (!data.empty() && data.front().second < accum.curTime - WINDOW_SIZE)
      data.erase(data.begin());
  };
  eraseOld(accum.inData);
  eraseOld(accum.outData);
}

int main(int argc, const char** argv)
{
  auto benchOptions = socketwire_examples::benchmark::parseOptions(argc, argv, 10131);
  socketwire_examples::benchmark::MetricsCollector metrics(
    benchOptions, "ship-swarm", "socketwire", "client");
  socketwire_examples::benchmark::setActiveCollector(&metrics);

  const std::uint16_t connectPort = benchOptions.enabled
    ? benchOptions.port
    : socketwire_examples::portFromArgsOrEnv(argc, argv, 1, "SOCKETWIRE_SHIP_SWARM_PORT", 10131);

  auto socket = socketwire_examples::createUdpSocket(0);
  if (socket == nullptr)
    return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire::ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.SetHandler(&handler);
  connection.Connect(socketwire_examples::resolveAddress(benchOptions.host), connectPort);

  int width = 600;
  int height = 600;

  if (!benchOptions.enabled)
    InitWindow(width, height, "Ship Swarm");

  if (!benchOptions.enabled)
  {
    const int scrWidth = GetMonitorWidth(0);
    const int scrHeight = GetMonitorHeight(0);
    if (scrWidth < width || scrHeight < height)
    {
      width = std::min(scrWidth, width);
      height = std::min(scrHeight - 150, height);
      SetWindowSize(width, height);
    }
  }

  Camera2D camera = {{0.f, 0.f}, {0.f, 0.f}, 0.f, 1.f};
  camera.offset = Vector2{width * 0.5f, height * 0.5f};
  camera.zoom = 10.f;

  if (!benchOptions.enabled)
    SetTargetFPS(60);

  bool sentJoin = false;
  BandwidthAccumulator bandwidthAccumulator;
  std::uint64_t benchFrame = 0;
  while (benchOptions.enabled ? !metrics.done() : !WindowShouldClose())
  {
    const auto frameStart = std::chrono::steady_clock::now();
    const float dt = benchOptions.enabled ? (1.f / 60.f) : GetFrameTime();
    const auto updateStart = std::chrono::steady_clock::now();
    connection.Tick();

    if (handler.connected && !sentJoin)
    {
      send_join(&connection);
      totalOutData += 1;
      sentJoin = true;
    }

    update_bandwidth(dt, bandwidthAccumulator);
    simulate_world(connection, benchOptions.enabled, benchOptions, benchFrame);
    const auto updateEnd = std::chrono::steady_clock::now();
    if (!benchOptions.enabled)
    {
      update_camera(camera);
      draw_world(camera, bandwidthAccumulator);
    }
    else
    {
      metrics.setConnectedClients(handler.connected ? 1 : 0);
      metrics.setNetworkStats(socketwire_examples::benchmark::statsFromConnection(connection));
      metrics.recordUpdateMs(static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(updateEnd - updateStart).count()) / 1000.0);
      metrics.recordFrameMs(static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - frameStart).count()) / 1000.0);
      metrics.maybeWriteSample();
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
      ++benchFrame;
    }
  }

  connection.Disconnect();
  metrics.finish();
  socketwire_examples::benchmark::setActiveCollector(nullptr);
  if (!benchOptions.enabled)
    CloseWindow();
  return 0;
}
