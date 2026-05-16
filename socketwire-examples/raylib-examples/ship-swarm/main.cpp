#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "benchmark_utils.hpp"
#include "entity.h"
#include "protocol.h"
#include "raylib.h"
#include "reliable_connection.hpp"
#include "socketwire_example_utils.hpp"
#include "windows_defines.hpp"  // IWYU pragma: keep

static std::vector<Entity> entities;
static std::unordered_map<std::uint16_t, std::size_t> index_map;
static std::uint16_t my_entity = kInvalidEntity;
static std::uint32_t total_in_data = 0;
static std::uint32_t total_out_data = 0;
static std::uint32_t server_time_msec = 0;

struct BandwidthAccumulator {
  std::vector<std::pair<std::uint32_t, float>> inData;
  std::vector<std::pair<std::uint32_t, float>> outData;
  float curTime = 0.f;
};

static std::uint32_t GetDeltaData(
  const std::vector<std::pair<std::uint32_t, float>>& data) {
  if (data.empty()) return 0;
  return data.back().first - data.front().first;
}

static void OnNewEntityPacket(const void* data, std::size_t size) {
  Entity new_entity;
  DeserializeNewEntity(data, size, new_entity);
  if (index_map.contains(new_entity.eid)) return;

  index_map[new_entity.eid] = entities.size();
  entities.push_back(new_entity);
}

static void OnSetControlledEntity(const void* data, std::size_t size) {
  DeserializeSetControlledEntity(data, size, my_entity);
}

template <typename Callable>
static void GetEntity(std::uint16_t eid, Callable callable) {
  const auto it = index_map.find(eid);
  if (it != index_map.end()) callable(entities[it->second]);
}

static void OnSnapshot(const void* data, std::size_t size) {
  std::uint16_t eid = kInvalidEntity;
  float x = 0.f;
  float y = 0.f;
  float ori = 0.f;
  DeserializeSnapshot(data, size, eid, x, y, ori);
  GetEntity(eid, [&](Entity& e) {
    e.x = x;
    e.y = y;
    e.ori = ori;
  });
}

static void OnTime(const void* data, std::size_t size) {
  DeserializeTimeMsec(data, size, server_time_msec);
}

static void DrawShip(float ship_len, float ship_width, float x, float y,
                     const Vector2& fwd, const Vector2& left, Color color) {
  DrawTriangle(
    Vector2{x + fwd.x * ship_len * 0.5f, y + fwd.y * ship_len * 0.5f},
    Vector2{x - fwd.x * ship_len * 0.5f - left.x * ship_width * 0.5f,
            y - fwd.y * ship_len * 0.5f - left.y * ship_width * 0.5f},
    Vector2{x - fwd.x * ship_len * 0.5f + left.x * ship_width * 0.5f,
            y - fwd.y * ship_len * 0.5f + left.y * ship_width * 0.5f},
    color);
}

static void DrawEntity(const Entity& e) {
  constexpr float ship_len = 3.f;
  constexpr float ship_width = 2.f;
  const Vector2 fwd = Vector2{std::cos(e.ori), std::sin(e.ori)};
  const Vector2 left = Vector2{-fwd.y, fwd.x};
  const Vector3 hsv = ColorToHSV(GetColor(e.color));
  DrawShip(
    ship_len + 0.4f, ship_width + 0.4f, e.x, e.y, fwd, left,
    ColorFromHSV(static_cast<float>(static_cast<int>(hsv.x + 120.f) % 360), 1.f,
                 1.f));
  DrawShip(ship_len, ship_width, e.x, e.y, fwd, left,
           ColorFromHSV(hsv.x, hsv.y, hsv.z));
}

class ClientHandler final : public socketwire::IReliableConnectionHandler {
 public:
  void OnConnected() override { connected = true; }
  void OnDisconnected() override { connected = false; }

  void OnReliableReceived(std::uint8_t channel, const void* data,
                          std::size_t size) override {
    socketwire_examples::benchmark::RecordPayloadRx(size);
    ProcessPacket(channel, data, size);
  }

  void OnUnreliableReceived(std::uint8_t channel, const void* data,
                            std::size_t size) override {
    socketwire_examples::benchmark::RecordPayloadRx(size);
    ProcessPacket(channel, data, size);
  }

  bool connected = false;

 private:
  static void ProcessPacket([[maybe_unused]] std::uint8_t channel,
                            const void* data, std::size_t size) {
    total_in_data += static_cast<std::uint32_t>(size);
    switch (GetPacketType(data, size)) {
      case kEServerToClientNewEntity:
        OnNewEntityPacket(data, size);
        break;
      case kEServerToClientSetControlledEntity:
        OnSetControlledEntity(data, size);
        break;
      case kEServerToClientSnapshot:
        OnSnapshot(data, size);
        break;
      case kEServerToClientTimeMsec:
        OnTime(data, size);
        break;
      case kEClientToServerJoin:
      case kEClientToServerInput:
        break;
    }
  }
};

static void SimulateWorld(
  socketwire::ReliableConnection& connection, bool bench_mode,
  const socketwire_examples::benchmark::Options& bench_options,
  std::uint64_t bench_frame) {
  if (my_entity == kInvalidEntity) return;

  GetEntity(my_entity, [&](Entity&) {
    const float thr = bench_mode
                        ? socketwire_examples::benchmark::DeterministicAxis(
                            bench_options.seed, bench_frame, 0)
                        : ((IsKeyDown(KEY_UP) ? 1.f : 0.f) +
                           (IsKeyDown(KEY_DOWN) ? -1.f : 0.f));
    const float steer = bench_mode
                          ? socketwire_examples::benchmark::DeterministicAxis(
                              bench_options.seed, bench_frame, 1)
                          : ((IsKeyDown(KEY_LEFT) ? -1.f : 0.f) +
                             (IsKeyDown(KEY_RIGHT) ? 1.f : 0.f));
    SendEntityInput(&connection, my_entity, thr, steer);
    total_out_data += static_cast<std::uint32_t>(
      sizeof(std::uint8_t) + sizeof(std::uint16_t) + sizeof(std::uint8_t));
  });
}

static void DrawWorld(const Camera2D& camera, const BandwidthAccumulator& bw) {
  BeginDrawing();
  ClearBackground(DARKGRAY);
  BeginMode2D(camera);

  DrawRectangleLines(-kWorldSize, -kWorldSize, 2.f * kWorldSize,
                     2.f * kWorldSize, WHITE);

  constexpr std::size_t num_grid = 10;
  for (std::size_t y = 1; y < num_grid; ++y) {
    DrawLine(
      -kWorldSize,
      -kWorldSize + 2.f * kWorldSize * (static_cast<float>(y) / num_grid),
      kWorldSize,
      -kWorldSize + 2.f * kWorldSize * (static_cast<float>(y) / num_grid),
      GetColor(0xffffffff));
  }

  for (std::size_t x = 1; x < num_grid; ++x) {
    DrawLine(
      -kWorldSize + 2.f * kWorldSize * (static_cast<float>(x) / num_grid),
      -kWorldSize,
      -kWorldSize + 2.f * kWorldSize * (static_cast<float>(x) / num_grid),
      kWorldSize, GetColor(0xffffffff));
  }

  for (const Entity& e : entities) DrawEntity(e);

  EndMode2D();
  DrawText(
    TextFormat("Bandwidth: in %0.2f kbit/s", GetDeltaData(bw.inData) / 1024.f),
    8, 8, 12, WHITE);
  DrawText(TextFormat("Bandwidth: out %0.2f kbit/s",
                      GetDeltaData(bw.outData) / 1024.f),
           8, 20, 12, WHITE);
  EndDrawing();
}

static void UpdateCamera(Camera2D& camera) {
  if (my_entity != kInvalidEntity) {
    GetEntity(my_entity, [&](Entity& e) {
      camera.target.x += (e.x - camera.target.x) * 0.1f;
      camera.target.y += (e.y - camera.target.y) * 0.1f;
    });
  }
  camera.zoom *= (1.f - GetMouseWheelMove() * 0.1f);
}

static void UpdateBandwidth(float dt, BandwidthAccumulator& accum) {
  constexpr float window_size = 1.f;
  accum.curTime += dt;
  accum.inData.emplace_back(total_in_data, accum.curTime);
  accum.outData.emplace_back(total_out_data, accum.curTime);

  auto erase_old = [&](std::vector<std::pair<std::uint32_t, float>>& data) {
    while (!data.empty() && data.front().second < accum.curTime - window_size) {
      data.erase(data.begin());
    }
  };
  erase_old(accum.inData);
  erase_old(accum.outData);
}

int main(int argc, const char** argv) {
  auto bench_options =
    socketwire_examples::benchmark::ParseOptions(argc, argv, 10131);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "ship-swarm", "socketwire", "client");
  socketwire_examples::benchmark::SetActiveCollector(&metrics);

  const std::uint16_t connect_port =
    bench_options.enabled
      ? bench_options.port
      : socketwire_examples::PortFromArgsOrEnv(
          argc, argv, 1, "SOCKETWIRE_SHIP_SWARM_PORT", 10131);

  auto socket = socketwire_examples::CreateUdpSocket(0);
  if (socket == nullptr) return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire::ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.SetHandler(&handler);
  connection.Connect(socketwire_examples::ResolveAddress(bench_options.host),
                     connect_port);

  int width = 600;
  int height = 600;

  if (!bench_options.enabled) InitWindow(width, height, "Ship Swarm");

  if (!bench_options.enabled) {
    const int scr_width = GetMonitorWidth(0);
    const int scr_height = GetMonitorHeight(0);
    if (scr_width < width || scr_height < height) {
      width = std::min(scr_width, width);
      height = std::min(scr_height - 150, height);
      SetWindowSize(width, height);
    }
  }

  Camera2D camera = {{0.f, 0.f}, {0.f, 0.f}, 0.f, 1.f};
  camera.offset = Vector2{width * 0.5f, height * 0.5f};
  camera.zoom = 10.f;

  if (!bench_options.enabled) SetTargetFPS(60);

  bool sent_join = false;
  BandwidthAccumulator bandwidth_accumulator;
  std::uint64_t bench_frame = 0;
  while (bench_options.enabled ? !metrics.Done() : !WindowShouldClose()) {
    const auto frame_start = std::chrono::steady_clock::now();
    const float dt = bench_options.enabled ? (1.f / 60.f) : GetFrameTime();
    const auto update_start = std::chrono::steady_clock::now();
    connection.Tick();

    if (handler.connected && !sent_join) {
      SendJoin(&connection);
      total_out_data += 1;
      sent_join = true;
    }

    UpdateBandwidth(dt, bandwidth_accumulator);
    SimulateWorld(connection, bench_options.enabled, bench_options,
                  bench_frame);
    const auto update_end = std::chrono::steady_clock::now();
    if (!bench_options.enabled) {
      UpdateCamera(camera);
      DrawWorld(camera, bandwidth_accumulator);
    } else {
      metrics.SetConnectedClients(handler.connected ? 1 : 0);
      metrics.SetNetworkStats(
        socketwire_examples::benchmark::StatsFromConnection(connection));
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
      ++bench_frame;
    }
  }

  connection.Disconnect();
  metrics.Finish();
  socketwire_examples::benchmark::SetActiveCollector(nullptr);
  if (!bench_options.enabled) CloseWindow();
  return 0;
}
