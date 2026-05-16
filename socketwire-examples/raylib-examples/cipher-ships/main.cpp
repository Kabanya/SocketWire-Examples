#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "benchmark_utils.hpp"
#include "entity.h"
#include "protocol.h"
#include "raylib.h"
#include "reliable_connection.hpp"
#include "socketwire_example_utils.hpp"
#include "windows_defines.hpp"  // IWYU pragma: keep

static std::vector<Entity> entities;
static std::uint16_t my_entity = kInvalidEntity;

static void OnNewEntityPacket(const void* data, std::size_t size) {
  Entity new_entity;
  DeserializeNewEntity(data, size, new_entity);
  for (const Entity& e : entities) {
    if (e.eid == new_entity.eid) return;
  }
  entities.push_back(new_entity);
}

static void OnSetControlledEntity(const void* data, std::size_t size) {
  DeserializeSetControlledEntity(data, size, my_entity);
}

static void OnSnapshot(const void* data, std::size_t size) {
  std::uint16_t eid = kInvalidEntity;
  float x = 0.f;
  float y = 0.f;
  float ori = 0.f;
  DeserializeSnapshot(data, size, eid, x, y, ori);

  for (Entity& e : entities) {
    if (e.eid == eid) {
      e.x = x;
      e.y = y;
      e.ori = ori;
      return;
    }
  }
}

static void OnKey(const void* data, std::size_t size) {
  DeserializeAndSetKey(data, size);
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
      case kEServerToClientKey:
        OnKey(data, size);
        break;
      case kEClientToServerJoin:
      case kEClientToServerInput:
        break;
    }
  }
};

int main(int argc, const char** argv) {
  auto bench_options =
    socketwire_examples::benchmark::ParseOptions(argc, argv, 10131);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "cipher-ships", "socketwire", "client");
  socketwire_examples::benchmark::SetActiveCollector(&metrics);

  const std::uint16_t connect_port =
    bench_options.enabled
      ? bench_options.port
      : socketwire_examples::PortFromArgsOrEnv(
          argc, argv, 1, "SOCKETWIRE_CIPHER_SHIPS_PORT", 10131);

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

  if (!bench_options.enabled) InitWindow(width, height, "Cipher Ships");

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
  std::uint64_t bench_frame = 0;
  while (bench_options.enabled ? !metrics.Done() : !WindowShouldClose()) {
    const auto frame_start = std::chrono::steady_clock::now();
    const auto update_start = std::chrono::steady_clock::now();
    connection.Tick();

    if (handler.connected && !sent_join) {
      SendJoin(&connection);
      sent_join = true;
    }

    if (my_entity != kInvalidEntity) {
      const float thr = bench_options.enabled
                          ? socketwire_examples::benchmark::DeterministicAxis(
                              bench_options.seed, bench_frame, 0)
                          : ((IsKeyDown(KEY_UP) ? 1.f : 0.f) +
                             (IsKeyDown(KEY_DOWN) ? -1.f : 0.f));
      const float steer = bench_options.enabled
                            ? socketwire_examples::benchmark::DeterministicAxis(
                                bench_options.seed, bench_frame, 1)
                            : ((IsKeyDown(KEY_LEFT) ? -1.f : 0.f) +
                               (IsKeyDown(KEY_RIGHT) ? 1.f : 0.f));

      for (Entity const& e : entities) {
        if (e.eid == my_entity) {
          SendEntityInput(&connection, my_entity, thr, steer);
        }
      }
    }
    const auto update_end = std::chrono::steady_clock::now();

    if (!bench_options.enabled) {
      BeginDrawing();
      ClearBackground(GRAY);
      BeginMode2D(camera);
      DrawRectangleLines(-16, -8, 32, 16, GetColor(0xff00ffff));
      for (const Entity& e : entities) {
        const Rectangle rect = {e.x, e.y, 3.f, 1.f};
        DrawRectanglePro(rect, {0.f, 0.5f}, e.ori * 180.f / PI,
                         GetColor(e.color));
      }
      EndMode2D();
      EndDrawing();
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
