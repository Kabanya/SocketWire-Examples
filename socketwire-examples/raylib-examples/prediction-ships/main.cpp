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
#include <deque>
#include <thread>
#include <unordered_map>
#include <vector>

struct InputCommand
{
  std::uint32_t frameNumber = 0;
  float thr = 0.f;
  float steer = 0.f;
  TimePoint timestamp;
};

struct EntityState
{
  float x = 0.f;
  float y = 0.f;
  float ori = 0.f;
  float vx = 0.f;
  float vy = 0.f;
  float omega = 0.f;
  std::uint32_t frameNumber = 0;
};

struct Snapshot
{
  std::uint16_t eid = INVALID_ENTITY;
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
static std::unordered_map<std::uint16_t, std::size_t> indexMap;
static std::uint16_t myEntity = INVALID_ENTITY;
static std::unordered_map<std::uint16_t, std::vector<Snapshot>> snapshotHistory;
static constexpr std::chrono::milliseconds INTERPOLATION_TIME{200};

static std::deque<InputCommand> inputHistory;
static std::uint32_t clientFrameCounter = 0;
static std::uint32_t lastAcknowledgedFrame = 0;
static bool pendingCorrection = false;
static Snapshot serverState;
static constexpr float PREDICTION_ERROR_THRESHOLD = 0.5f;
static std::uint32_t estimatedServerTimeMsec = 0;

static std::deque<EntityState> stateHistory;
static constexpr std::size_t STATE_HISTORY_LIMIT = 200;

static void on_new_entity_packet(const void* data, std::size_t size)
{
  Entity newEntity;
  deserialize_new_entity(data, size, newEntity);
  if (indexMap.contains(newEntity.eid))
    return;

  std::printf("Received new entity with ID: %u\n", newEntity.eid);
  indexMap[newEntity.eid] = entities.size();
  entities.push_back(newEntity);
}

static void on_set_controlled_entity(const void* data, std::size_t size)
{
  deserialize_set_controlled_entity(data, size, myEntity);
  std::printf("Set controlled entity to: %u\n", myEntity);
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
  float vx = 0.f;
  float vy = 0.f;
  float omega = 0.f;
  TimePoint timestamp;
  std::uint32_t frameNumber = 0;

  deserialize_snapshot(data, size, eid, x, y, ori, vx, vy, omega, timestamp, frameNumber);
  const Snapshot snapshot{eid, x, y, ori, vx, vy, omega, timestamp, frameNumber};

  if (eid == myEntity)
  {
    serverState = snapshot;
    lastAcknowledgedFrame = frameNumber;

    while (!inputHistory.empty() && inputHistory.front().frameNumber <= frameNumber)
      inputHistory.pop_front();

    get_entity(myEntity, [&](Entity& e)
    {
      const float dx = e.x - x;
      const float dy = e.y - y;
      const float posError = std::sqrt(dx * dx + dy * dy);
      if (posError > PREDICTION_ERROR_THRESHOLD)
        pendingCorrection = true;
    });
  }

  snapshotHistory[eid].push_back(snapshot);
  auto& snapshots = snapshotHistory[eid];
  if (snapshots.size() > 1 && snapshots.back().frameNumber < snapshots[snapshots.size() - 2].frameNumber)
  {
    std::sort(snapshots.begin(), snapshots.end(),
      [](const Snapshot& a, const Snapshot& b) { return a.frameNumber < b.frameNumber; });
  }
}

static void process_snapshot_history(const TimePoint& currentTime)
{
  const TimePoint targetTime = currentTime - INTERPOLATION_TIME;

  for (auto& [eid, snapshots] : snapshotHistory)
  {
    if (snapshots.empty())
      continue;

    while (snapshots.size() > 2 && snapshots[1].timestamp < targetTime)
      snapshots.erase(snapshots.begin());

    if (snapshots.size() < 2 || targetTime <= snapshots[0].timestamp)
    {
      const auto& snapshot = snapshots[0];
      get_entity(eid, [&](Entity& e)
      {
        e.x = snapshot.x;
        e.y = snapshot.y;
        e.ori = snapshot.ori;
      });
      continue;
    }

    std::size_t index = 0;
    while (index < snapshots.size() - 1 && snapshots[index + 1].timestamp <= targetTime)
      ++index;

    if (index >= snapshots.size() - 1)
    {
      const auto& snapshot = snapshots.back();
      get_entity(eid, [&](Entity& e)
      {
        e.x = snapshot.x;
        e.y = snapshot.y;
        e.ori = snapshot.ori;
      });
      continue;
    }

    const auto& s1 = snapshots[index];
    const auto& s2 = snapshots[index + 1];

    float t = 0.f;
    const auto s2MinusS1 = s2.timestamp - s1.timestamp;
    const auto targetMinusS1 = targetTime - s1.timestamp;
    if (s2MinusS1.count() > 0)
    {
      t = static_cast<float>(targetMinusS1.count()) / static_cast<float>(s2MinusS1.count());
      t = std::clamp(t, 0.f, 1.f);
    }

    const float interpX = s1.x + (s2.x - s1.x) * t;
    const float interpY = s1.y + (s2.y - s1.y) * t;

    float dOri = s2.ori - s1.ori;
    if (dOri > 3.14159f)
      dOri -= 2.f * 3.14159f;
    else if (dOri < -3.14159f)
      dOri += 2.f * 3.14159f;

    const float interpOri = s1.ori + dOri * t;
    get_entity(eid, [&](Entity& e)
    {
      e.x = interpX;
      e.y = interpY;
      e.ori = interpOri;
    });
  }
}

static void on_time(const void* data, std::size_t size, const socketwire::ReliableConnection& connection)
{
  std::uint32_t timeMsec = 0;
  deserialize_time_msec(data, size, timeMsec);
  estimatedServerTimeMsec = timeMsec + static_cast<std::uint32_t>(connection.GetRtt() * 0.5f);
}

static void draw_entity(const Entity& e)
{
  constexpr float SHIP_LEN = 3.f;
  constexpr float SHIP_WIDTH = 2.f;
  const Vector2 fwd = Vector2{std::cos(e.ori), std::sin(e.ori)};
  const Vector2 left = Vector2{-fwd.y, fwd.x};
  DrawTriangle(Vector2{e.x + fwd.x * SHIP_LEN * 0.5f, e.y + fwd.y * SHIP_LEN * 0.5f},
               Vector2{e.x - fwd.x * SHIP_LEN * 0.5f - left.x * SHIP_WIDTH * 0.5f,
                       e.y - fwd.y * SHIP_LEN * 0.5f - left.y * SHIP_WIDTH * 0.5f},
               Vector2{e.x - fwd.x * SHIP_LEN * 0.5f + left.x * SHIP_WIDTH * 0.5f,
                       e.y - fwd.y * SHIP_LEN * 0.5f + left.y * SHIP_WIDTH * 0.5f},
               GetColor(e.color));
}

class ClientHandler final : public socketwire::IReliableConnectionHandler
{
public:
  explicit ClientHandler(socketwire::ReliableConnection& connection)
    : connection_(connection)
  {
  }

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
  socketwire::ReliableConnection& connection_;

  void processPacket([[maybe_unused]] std::uint8_t channel, const void* data, std::size_t size)
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
      case E_SERVER_TO_CLIENT_TIME_MSEC:
        on_time(data, size, connection_);
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

  const float thr = benchMode
    ? socketwire_examples::benchmark::deterministicAxis(benchOptions.seed, benchFrame, 0)
    : ((IsKeyDown(KEY_UP) ? 1.f : 0.f) + (IsKeyDown(KEY_DOWN) ? -1.f : 0.f));
  const float steer = benchMode
    ? socketwire_examples::benchmark::deterministicAxis(benchOptions.seed, benchFrame, 1)
    : ((IsKeyDown(KEY_LEFT) ? -1.f : 0.f) + (IsKeyDown(KEY_RIGHT) ? 1.f : 0.f));

  inputHistory.push_back(InputCommand{clientFrameCounter, thr, steer, std::chrono::steady_clock::now()});
  while (inputHistory.size() > 100)
    inputHistory.pop_front();

  send_entity_input(&connection, myEntity, thr, steer);

  get_entity(myEntity, [&](Entity& e)
  {
    if (pendingCorrection)
    {
      const auto it = std::find_if(stateHistory.begin(), stateHistory.end(),
        [](const EntityState& state) { return state.frameNumber == serverState.frameNumber; });
      if (it != stateHistory.end())
      {
        const float dx = serverState.x - it->x;
        const float dy = serverState.y - it->y;
        const float dvx = serverState.vx - it->vx;
        const float dvy = serverState.vy - it->vy;
        const float dori = serverState.ori - it->ori;
        const float domega = serverState.omega - it->omega;
        for (auto jt = it; jt != stateHistory.end(); ++jt)
        {
          jt->x += dx;
          jt->y += dy;
          jt->vx += dvx;
          jt->vy += dvy;
          jt->ori += dori;
          jt->omega += domega;
        }
      }

      e.x = serverState.x;
      e.y = serverState.y;
      e.vx = serverState.vx;
      e.vy = serverState.vy;
      e.ori = serverState.ori;
      e.omega = serverState.omega;

      for (const auto& input : inputHistory)
      {
        e.thr = input.thr;
        e.steer = input.steer;
        simulate_entity(e, FIXED_DT);
      }
      pendingCorrection = false;
    }
    else
    {
      e.thr = thr;
      e.steer = steer;
      simulate_entity(e, FIXED_DT);
    }

    stateHistory.push_back(EntityState{e.x, e.y, e.ori, e.vx, e.vy, e.omega, clientFrameCounter});
    if (stateHistory.size() > STATE_HISTORY_LIMIT)
      stateHistory.pop_front();
  });
}

static void draw_world(const Camera2D& camera)
{
  BeginDrawing();
    ClearBackground(GRAY);
    BeginMode2D(camera);

      for (const Entity& e : entities)
        draw_entity(e);

    EndMode2D();

    if (myEntity != INVALID_ENTITY)
    {
      char buffer[320]{};
      get_entity(myEntity, [&](const Entity& e)
      {
        std::snprintf(buffer,
                      sizeof(buffer),
                      "Pos: (%+2.2f, %+2.2f)\n"
                      "Vel: (%+2.2f, %+2.2f)\n"
                      "Ori: %+2.2f  Omega: %+2.2f\n"
                      "Thr: %+1.2f  Steer: %+1.2f\n"
                      "Frame: %3u | Last server frame: %3u\n"
                      "InputHist: %zu | StateHist: %zu\n"
                      "PendingCorrection: %s\n"
                      "Server Delay: 200 ms",
                      e.x,
                      e.y,
                      e.vx,
                      e.vy,
                      e.ori,
                      e.omega,
                      e.thr,
                      e.steer,
                      clientFrameCounter,
                      lastAcknowledgedFrame,
                      inputHistory.size(),
                      stateHistory.size(),
                      pendingCorrection ? "YES" : "NO");
      });
      DrawText(buffer, 5, 5, 10, BLACK);
    }

  EndDrawing();
}

int main(int argc, const char** argv)
{
  auto benchOptions = socketwire_examples::benchmark::parseOptions(argc, argv, 10131);
  socketwire_examples::benchmark::MetricsCollector metrics(
    benchOptions, "prediction-ships", "socketwire", "client");
  socketwire_examples::benchmark::setActiveCollector(&metrics);

  const std::uint16_t connectPort = benchOptions.enabled
    ? benchOptions.port
    : socketwire_examples::portFromArgsOrEnv(argc, argv, 1, "SOCKETWIRE_PREDICTION_SHIPS_PORT", 10131);

  auto socket = socketwire_examples::createUdpSocket(0);
  if (socket == nullptr)
    return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire::ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler(connection);
  connection.SetHandler(&handler);
  connection.Connect(socketwire_examples::resolveAddress(benchOptions.host), connectPort);

  int width = 600;
  int height = 600;

  if (!benchOptions.enabled)
    InitWindow(width, height, "Prediction Ships");

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

  float accumulator = 0.f;
  clientFrameCounter = 0;
  bool sentJoin = false;
  std::uint64_t benchFrame = 0;

  while (benchOptions.enabled ? !metrics.done() : !WindowShouldClose())
  {
    const auto frameStart = std::chrono::steady_clock::now();
    const float frameTime = benchOptions.enabled ? (1.f / 60.f) : GetFrameTime();
    accumulator += frameTime;

    const auto updateStart = std::chrono::steady_clock::now();
    connection.Tick();
    if (handler.connected && !sentJoin)
    {
      send_join(&connection);
      sentJoin = true;
    }

    while (accumulator >= FIXED_DT)
    {
      simulate_world(connection, benchOptions.enabled, benchOptions, benchFrame);
      ++clientFrameCounter;
      ++benchFrame;
      accumulator -= FIXED_DT;
    }

    process_snapshot_history(std::chrono::steady_clock::now());
    const auto updateEnd = std::chrono::steady_clock::now();
    if (!benchOptions.enabled)
    {
      draw_world(camera);
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
    }
  }

  connection.Disconnect();
  metrics.finish();
  socketwire_examples::benchmark::setActiveCollector(nullptr);
  if (!benchOptions.enabled)
    CloseWindow();
  return 0;
}
