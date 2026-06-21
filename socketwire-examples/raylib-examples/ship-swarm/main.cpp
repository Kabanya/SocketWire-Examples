#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <print>
#include <string>
#include <string_view>
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

static constexpr std::uint16_t kDefaultShipSwarmPort = 10133;

struct BandwidthAccumulator {
  std::vector<std::pair<std::uint32_t, float>> inData;
  std::vector<std::pair<std::uint32_t, float>> outData;
  float curTime = 0.f;
};

struct ClientEndpoint {
  std::string host = "127.0.0.1";
  std::uint16_t port = kDefaultShipSwarmPort;
};

static bool IsOptionWithValue(std::string_view option) {
  return option == "--host" || option == "--port" || option == "--lobby-port" ||
         option == "--game-port" || option == "--duration-ms" ||
         option == "--warmup-ms" || option == "--seed" ||
         option == "--metrics" || option == "--metrics-mode" ||
         option == "--clients" || option == "--run";
}

static std::vector<std::string_view> CollectPositionals(int argc,
                                                        const char** argv) {
  std::vector<std::string_view> positionals;
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == nullptr) continue;

    const std::string_view arg(argv[i]);
    if (socketwire_examples::IsCommandLineOption(argv[i])) {
      if (IsOptionWithValue(arg) && i + 1 < argc) ++i;
      continue;
    }

    positionals.push_back(arg);
  }
  return positionals;
}

static ClientEndpoint ResolveClientEndpoint(
  int argc, const char** argv,
  const socketwire_examples::benchmark::Options& bench_options) {
  ClientEndpoint endpoint{bench_options.host, bench_options.port};
  if (bench_options.enabled) return endpoint;

  endpoint = ClientEndpoint{};

  if (const char* env_host = std::getenv("SOCKETWIRE_SHIP_SWARM_HOST");
      env_host != nullptr && *env_host != '\0') {
    endpoint.host = env_host;
  }

  if (const char* env_port = std::getenv("SOCKETWIRE_SHIP_SWARM_PORT");
      env_port != nullptr && *env_port != '\0') {
    if (const auto parsed = socketwire_examples::ParsePort(env_port);
        parsed.has_value()) {
      endpoint.port = *parsed;
    } else {
      std::println(
        "Ignoring invalid port in SOCKETWIRE_SHIP_SWARM_PORT='{}'; "
        "using {}",
        env_port, static_cast<unsigned>(endpoint.port));
    }
  }

  if (socketwire_examples::HasCommandLineOption(argc, argv, "--host")) {
    endpoint.host = bench_options.host;
  }
  if (socketwire_examples::HasCommandLineOption(argc, argv, "--port")) {
    endpoint.port = bench_options.port;
  }

  const std::vector<std::string_view> positionals =
    CollectPositionals(argc, argv);
  if (!positionals.empty()) {
    if (const auto parsed = socketwire_examples::ParsePort(positionals[0]);
        parsed.has_value()) {
      endpoint.port = *parsed;
    } else {
      endpoint.host = std::string(positionals[0]);
    }
  }
  if (positionals.size() >= 2) {
    if (const auto parsed = socketwire_examples::ParsePort(positionals[1]);
        parsed.has_value()) {
      endpoint.port = *parsed;
    } else {
      std::println("Ignoring invalid ship-swarm port argument '{}'; using {}",
                   positionals[1], static_cast<unsigned>(endpoint.port));
    }
  }

  return endpoint;
}

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

static void DrawWorld(const Camera2D& camera, const BandwidthAccumulator& bw,
                      const ClientEndpoint& endpoint,
                      const ClientHandler& handler) {
  BeginDrawing();
  ClearBackground(DARKGRAY);
  BeginMode2D(camera);

  DrawRectangleLines(-kWorldSize, -kWorldSize, 2.f * kWorldSize,
                     2.f * kWorldSize, WHITE);

  constexpr std::size_t num_grid = 10;
  for (std::size_t y = 1; y < num_grid; ++y) {
    const float grid_fraction =
      static_cast<float>(y) / static_cast<float>(num_grid);
    const float y_pos = -kWorldSize + 2.f * kWorldSize * grid_fraction;
    DrawLineV(Vector2{-kWorldSize, y_pos}, Vector2{kWorldSize, y_pos},
              GetColor(0xffffffff));
  }

  for (std::size_t x = 1; x < num_grid; ++x) {
    const float grid_fraction =
      static_cast<float>(x) / static_cast<float>(num_grid);
    const float x_pos = -kWorldSize + 2.f * kWorldSize * grid_fraction;
    DrawLineV(Vector2{x_pos, -kWorldSize}, Vector2{x_pos, kWorldSize},
              GetColor(0xffffffff));
  }

  for (const Entity& e : entities) DrawEntity(e);

  EndMode2D();
  DrawText(TextFormat("Bandwidth: in %0.2f kbit/s",
                      static_cast<float>(GetDeltaData(bw.inData)) / 1024.f),
           8, 8, 12, WHITE);
  DrawText(TextFormat("Bandwidth: out %0.2f kbit/s",
                      static_cast<float>(GetDeltaData(bw.outData)) / 1024.f),
           8, 20, 12, WHITE);
  DrawText(TextFormat("%s %s:%u | entities %zu",
                      handler.connected ? "Connected to" : "Connecting to",
                      endpoint.host.c_str(),
                      static_cast<unsigned>(endpoint.port), entities.size()),
           8, 32, 12, WHITE);
  if (handler.connected && entities.empty()) {
    DrawText("Waiting for world state", 8, 44, 12, LIGHTGRAY);
  }
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
  auto bench_options = socketwire_examples::benchmark::ParseOptions(
    argc, argv, kDefaultShipSwarmPort);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "ship-swarm", "socketwire", "client");
  socketwire_examples::benchmark::SetActiveCollector(&metrics);

  const ClientEndpoint endpoint =
    ResolveClientEndpoint(argc, argv, bench_options);
  auto server_endpoint =
    socketwire_examples::ResolveEndpoint(endpoint.host, endpoint.port);
  if (!server_endpoint) {
    std::println("cannot resolve host '{}'", endpoint.host);
    return 1;
  }

  auto socket = socketwire_examples::CreateUdpSocket(0);
  if (socket == nullptr) return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire::ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.SetHandler(&handler);
  if (!socketwire_examples::ConnectNextAddress(connection, *server_endpoint,
                                               endpoint.port)) {
    std::println("cannot connect to '{}'", endpoint.host);
    return 1;
  }
  std::println("ship-swarm client connecting to {}:{}", endpoint.host,
               static_cast<unsigned>(endpoint.port));

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
  camera.offset = Vector2{static_cast<float>(width) * 0.5f,
                          static_cast<float>(height) * 0.5f};
  camera.zoom =
    static_cast<float>(std::min(width, height)) / (2.f * kWorldSize) * 0.8f;

  if (!bench_options.enabled) SetTargetFPS(60);

  bool sent_join = false;
  auto next_connect_attempt =
    std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
  BandwidthAccumulator bandwidth_accumulator;
  std::uint64_t bench_frame = 0;
  while (bench_options.enabled ? !metrics.Done() : !WindowShouldClose()) {
    const auto frame_start = std::chrono::steady_clock::now();
    const float dt = bench_options.enabled ? (1.f / 60.f) : GetFrameTime();
    const auto update_start = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (!handler.connected && now >= next_connect_attempt) {
      (void)socketwire_examples::ConnectNextAddress(connection,
                                                    *server_endpoint,
                                                    endpoint.port);
      next_connect_attempt = now + std::chrono::milliseconds(250);
    }
    connection.Update();

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
      DrawWorld(camera, bandwidth_accumulator, endpoint, handler);
    } else {
      metrics.SetConnectedClients(handler.connected ? 1 : 0);
      metrics.SetNetworkStats(
        socketwire_examples::benchmark::StatsFromConnection(connection));
      socketwire_examples::benchmark::GameMetrics game_metrics;
      game_metrics.joinSuccessCount = my_entity != kInvalidEntity ? 1 : 0;
      game_metrics.entityCountClient = entities.size();
      metrics.SetGameMetrics(game_metrics);
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
