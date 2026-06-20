#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <print>
#include <thread>
#include <unordered_map>
#include <vector>

#include "benchmark_utils.hpp"
#include "entity.h"
#include "protocol.h"
#include "raylib.h"
#include "reliable_connection.hpp"
#include "socketwire_example_utils.hpp"
#include "windows_defines.hpp"  // IWYU pragma: keep

struct InputCommand {
  std::uint32_t frameNumber = 0;
  float thr = 0.f;
  float steer = 0.f;
  TimePoint timestamp;
};

struct EntityState {
  float x = 0.f;
  float y = 0.f;
  float ori = 0.f;
  float vx = 0.f;
  float vy = 0.f;
  float omega = 0.f;
  std::uint32_t frameNumber = 0;
};

struct Snapshot {
  std::uint16_t eid = kInvalidEntity;
  float x = 0.f;
  float y = 0.f;
  float ori = 0.f;
  float vx = 0.f;
  float vy = 0.f;
  float omega = 0.f;
  TimePoint timestamp;
  std::uint32_t frameNumber = 0;
};

static std::vector<Entity> entities;
static std::unordered_map<std::uint16_t, std::size_t> index_map;
static std::uint16_t my_entity = kInvalidEntity;
static std::unordered_map<std::uint16_t, std::vector<Snapshot>>
  snapshot_history;
static constexpr std::chrono::milliseconds kInterpolationTime{200};

static std::deque<InputCommand> input_history;
static std::uint32_t client_frame_counter = 0;
static std::uint32_t last_acknowledged_frame = 0;
static bool pending_correction = false;
static Snapshot server_state;
static constexpr float kPredictionErrorThreshold = 0.5f;
static std::uint32_t estimated_server_time_msec = 0;

static std::deque<EntityState> state_history;
static constexpr std::size_t kStateHistoryLimit = 200;

static void OnNewEntityPacket(const void* data, std::size_t size) {
  Entity new_entity;
  DeserializeNewEntity(data, size, new_entity);
  if (index_map.contains(new_entity.eid)) return;

  std::println("Received new entity with ID: {}", new_entity.eid);
  index_map[new_entity.eid] = entities.size();
  entities.push_back(new_entity);
}

static void OnSetControlledEntity(const void* data, std::size_t size) {
  DeserializeSetControlledEntity(data, size, my_entity);
  std::println("Set controlled entity to: {}", my_entity);
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
  float vx = 0.f;
  float vy = 0.f;
  float omega = 0.f;
  TimePoint timestamp;
  std::uint32_t frame_number = 0;

  DeserializeSnapshot(data, size, eid, x, y, ori, vx, vy, omega, timestamp,
                      frame_number);
  const Snapshot snapshot{eid, x,     y,         ori,         vx,
                          vy,  omega, timestamp, frame_number};

  if (eid == my_entity) {
    server_state = snapshot;
    last_acknowledged_frame = frame_number;

    while (!input_history.empty() &&
           input_history.front().frameNumber <= frame_number) {
      input_history.pop_front();
    }

    GetEntity(my_entity, [&](Entity& e) {
      const float dx = e.x - x;
      const float dy = e.y - y;
      const float pos_error = std::sqrt(dx * dx + dy * dy);
      if (pos_error > kPredictionErrorThreshold) pending_correction = true;
    });
  }

  snapshot_history[eid].push_back(snapshot);
  auto& snapshots = snapshot_history[eid];
  if (snapshots.size() > 1 && snapshots.back().frameNumber <
                                snapshots[snapshots.size() - 2].frameNumber) {
    std::sort(snapshots.begin(), snapshots.end(),
              [](const Snapshot& a, const Snapshot& b) {
                return a.frameNumber < b.frameNumber;
              });
  }
}

static void ProcessSnapshotHistory(const TimePoint& current_time) {
  const TimePoint target_time = current_time - kInterpolationTime;

  for (auto& [eid, snapshots] : snapshot_history) {
    if (snapshots.empty()) continue;

    while (snapshots.size() > 2 && snapshots[1].timestamp < target_time) {
      snapshots.erase(snapshots.begin());
    }

    if (snapshots.size() < 2 || target_time <= snapshots[0].timestamp) {
      const auto& snapshot = snapshots[0];
      GetEntity(eid, [&](Entity& e) {
        e.x = snapshot.x;
        e.y = snapshot.y;
        e.ori = snapshot.ori;
      });
      continue;
    }

    std::size_t index = 0;
    while (index < snapshots.size() - 1 &&
           snapshots[index + 1].timestamp <= target_time) {
      ++index;
    }

    if (index >= snapshots.size() - 1) {
      const auto& snapshot = snapshots.back();
      GetEntity(eid, [&](Entity& e) {
        e.x = snapshot.x;
        e.y = snapshot.y;
        e.ori = snapshot.ori;
      });
      continue;
    }

    const auto& s1 = snapshots[index];
    const auto& s2 = snapshots[index + 1];

    float t = 0.f;
    const auto s2_minus_s1 = s2.timestamp - s1.timestamp;
    const auto target_minus_s1 = target_time - s1.timestamp;
    if (s2_minus_s1.count() > 0) {
      t = static_cast<float>(target_minus_s1.count()) /
          static_cast<float>(s2_minus_s1.count());
      t = std::clamp(t, 0.f, 1.f);
    }

    const float interp_x = s1.x + (s2.x - s1.x) * t;
    const float interp_y = s1.y + (s2.y - s1.y) * t;

    float d_ori = s2.ori - s1.ori;
    if (d_ori > 3.14159f) {
      d_ori -= 2.f * 3.14159f;
    } else if (d_ori < -3.14159f) {
      d_ori += 2.f * 3.14159f;
    }

    const float interp_ori = s1.ori + d_ori * t;
    GetEntity(eid, [&](Entity& e) {
      e.x = interp_x;
      e.y = interp_y;
      e.ori = interp_ori;
    });
  }
}

static void OnTime(const void* data, std::size_t size,
                   const socketwire::ReliableConnection& connection) {
  std::uint32_t time_msec = 0;
  DeserializeTimeMsec(data, size, time_msec);
  estimated_server_time_msec =
    time_msec + static_cast<std::uint32_t>(connection.GetRtt() * 0.5f);
}

static void DrawEntity(const Entity& e) {
  constexpr float ship_len = 3.f;
  constexpr float ship_width = 2.f;
  const Vector2 fwd = Vector2{std::cos(e.ori), std::sin(e.ori)};
  const Vector2 left = Vector2{-fwd.y, fwd.x};
  DrawTriangle(
    Vector2{e.x + fwd.x * ship_len * 0.5f, e.y + fwd.y * ship_len * 0.5f},
    Vector2{e.x - fwd.x * ship_len * 0.5f - left.x * ship_width * 0.5f,
            e.y - fwd.y * ship_len * 0.5f - left.y * ship_width * 0.5f},
    Vector2{e.x - fwd.x * ship_len * 0.5f + left.x * ship_width * 0.5f,
            e.y - fwd.y * ship_len * 0.5f + left.y * ship_width * 0.5f},
    GetColor(e.color));
}

class ClientHandler final : public socketwire::IReliableConnectionHandler {
 public:
  explicit ClientHandler(socketwire::ReliableConnection& connection)
      : connection_(connection) {}

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
  socketwire::ReliableConnection& connection_;

  void ProcessPacket([[maybe_unused]] std::uint8_t channel, const void* data,
                     std::size_t size) {
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
        OnTime(data, size, connection_);
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

  const float thr =
    bench_mode
      ? socketwire_examples::benchmark::DeterministicAxis(bench_options.seed,
                                                          bench_frame, 0)
      : ((IsKeyDown(KEY_UP) ? 1.f : 0.f) + (IsKeyDown(KEY_DOWN) ? -1.f : 0.f));
  const float steer = bench_mode
                        ? socketwire_examples::benchmark::DeterministicAxis(
                            bench_options.seed, bench_frame, 1)
                        : ((IsKeyDown(KEY_LEFT) ? -1.f : 0.f) +
                           (IsKeyDown(KEY_RIGHT) ? 1.f : 0.f));

  input_history.push_back(InputCommand{client_frame_counter, thr, steer,
                                       std::chrono::steady_clock::now()});
  while (input_history.size() > 100) input_history.pop_front();

  SendEntityInput(&connection, my_entity, thr, steer);

  GetEntity(my_entity, [&](Entity& e) {
    if (pending_correction) {
      const auto it =
        std::find_if(state_history.begin(), state_history.end(),
                     [](const EntityState& state) {
                       return state.frameNumber == server_state.frameNumber;
                     });
      if (it != state_history.end()) {
        const float dx = server_state.x - it->x;
        const float dy = server_state.y - it->y;
        const float dvx = server_state.vx - it->vx;
        const float dvy = server_state.vy - it->vy;
        const float dori = server_state.ori - it->ori;
        const float domega = server_state.omega - it->omega;
        for (auto jt = it; jt != state_history.end(); ++jt) {
          jt->x += dx;
          jt->y += dy;
          jt->vx += dvx;
          jt->vy += dvy;
          jt->ori += dori;
          jt->omega += domega;
        }
      }

      e.x = server_state.x;
      e.y = server_state.y;
      e.vx = server_state.vx;
      e.vy = server_state.vy;
      e.ori = server_state.ori;
      e.omega = server_state.omega;

      for (const auto& input : input_history) {
        e.thr = input.thr;
        e.steer = input.steer;
        SimulateEntity(e, kFixedDt);
      }
      pending_correction = false;
    } else {
      e.thr = thr;
      e.steer = steer;
      SimulateEntity(e, kFixedDt);
    }

    state_history.push_back(
      EntityState{e.x, e.y, e.ori, e.vx, e.vy, e.omega, client_frame_counter});
    if (state_history.size() > kStateHistoryLimit) state_history.pop_front();
  });
}

static void DrawWorld(const Camera2D& camera) {
  BeginDrawing();
  ClearBackground(GRAY);
  BeginMode2D(camera);

  for (const Entity& e : entities) DrawEntity(e);

  EndMode2D();

  if (my_entity != kInvalidEntity) {
    char buffer[320]{};
    GetEntity(my_entity, [&](const Entity& e) {
      std::snprintf(buffer, sizeof(buffer),
                    "Pos: (%+2.2f, %+2.2f)\n"
                    "Vel: (%+2.2f, %+2.2f)\n"
                    "Ori: %+2.2f  Omega: %+2.2f\n"
                    "Thr: %+1.2f  Steer: %+1.2f\n"
                    "Frame: %3u | Last server frame: %3u\n"
                    "InputHist: %zu | StateHist: %zu\n"
                    "PendingCorrection: %s\n"
                    "Server Delay: 200 ms",
                    e.x, e.y, e.vx, e.vy, e.ori, e.omega, e.thr, e.steer,
                    client_frame_counter, last_acknowledged_frame,
                    input_history.size(), state_history.size(),
                    pending_correction ? "YES" : "NO");
    });
    DrawText(buffer, 5, 5, 10, BLACK);
  }

  EndDrawing();
}

int main(int argc, const char** argv) {
  auto bench_options =
    socketwire_examples::benchmark::ParseOptions(argc, argv, 10131);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "prediction-ships", "socketwire", "client");
  socketwire_examples::benchmark::SetActiveCollector(&metrics);

  const std::uint16_t connect_port =
    bench_options.enabled
      ? bench_options.port
      : socketwire_examples::PortFromArgsOrEnv(
          argc, argv, 1, "SOCKETWIRE_PREDICTION_SHIPS_PORT", 10131);
  const auto server_address =
    socketwire_examples::ResolveAddress(bench_options.host);
  if (!server_address) {
    std::println("cannot resolve host '{}'", bench_options.host);
    return 1;
  }

  auto socket = socketwire_examples::CreateUdpSocket(0);
  if (socket == nullptr) return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire::ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler(connection);
  connection.SetHandler(&handler);
  connection.Connect(*server_address, connect_port);

  int width = 600;
  int height = 600;

  if (!bench_options.enabled) InitWindow(width, height, "Prediction Ships");

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
  camera.zoom = 10.f;

  if (!bench_options.enabled) SetTargetFPS(60);

  float accumulator = 0.f;
  client_frame_counter = 0;
  bool sent_join = false;
  std::uint64_t bench_frame = 0;

  while (bench_options.enabled ? !metrics.Done() : !WindowShouldClose()) {
    const auto frame_start = std::chrono::steady_clock::now();
    const float frame_time =
      bench_options.enabled ? (1.f / 60.f) : GetFrameTime();
    accumulator += frame_time;

    const auto update_start = std::chrono::steady_clock::now();
    connection.Update();
    if (handler.connected && !sent_join) {
      SendJoin(&connection);
      sent_join = true;
    }

    while (accumulator >= kFixedDt) {
      SimulateWorld(connection, bench_options.enabled, bench_options,
                    bench_frame);
      ++client_frame_counter;
      ++bench_frame;
      accumulator -= kFixedDt;
    }

    ProcessSnapshotHistory(std::chrono::steady_clock::now());
    const auto update_end = std::chrono::steady_clock::now();
    if (!bench_options.enabled) {
      DrawWorld(camera);
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
    }
  }

  connection.Disconnect();
  metrics.Finish();
  socketwire_examples::benchmark::SetActiveCollector(nullptr);
  if (!bench_options.enabled) CloseWindow();
  return 0;
}
