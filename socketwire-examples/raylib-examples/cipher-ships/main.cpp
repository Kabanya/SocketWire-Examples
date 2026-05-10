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
#include <vector>

static std::vector<Entity> entities;
static std::uint16_t myEntity = INVALID_ENTITY;

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
  std::uint16_t eid = INVALID_ENTITY;
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
    socketwire_examples::benchmark::recordPayloadRx(size);
    processPacket(channel, data, size);
  }

  void onUnreliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
  {
    socketwire_examples::benchmark::recordPayloadRx(size);
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

int main(int argc, const char** argv)
{
  auto benchOptions = socketwire_examples::benchmark::parseOptions(argc, argv, 10131);
  socketwire_examples::benchmark::MetricsCollector metrics(
    benchOptions, "cipher-ships", "socketwire", "client");
  socketwire_examples::benchmark::setActiveCollector(&metrics);

  const std::uint16_t connectPort = benchOptions.enabled
    ? benchOptions.port
    : socketwire_examples::portFromArgsOrEnv(argc, argv, 1, "SOCKETWIRE_CIPHER_SHIPS_PORT", 10131);

  auto socket = socketwire_examples::createUdpSocket(0);
  if (socket == nullptr)
    return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire::ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.setHandler(&handler);
  connection.connect(socketwire_examples::resolveAddress(benchOptions.host), connectPort);

  int width = 600;
  int height = 600;

  if (!benchOptions.enabled)
    InitWindow(width, height, "Cipher Ships");

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
  std::uint64_t benchFrame = 0;
  while (benchOptions.enabled ? !metrics.done() : !WindowShouldClose())
  {
    const auto frameStart = std::chrono::steady_clock::now();
    const auto updateStart = std::chrono::steady_clock::now();
    connection.tick();

    if (handler.connected && !sentJoin)
    {
      send_join(&connection);
      sentJoin = true;
    }

    if (myEntity != INVALID_ENTITY)
    {
      const float thr = benchOptions.enabled
        ? socketwire_examples::benchmark::deterministicAxis(benchOptions.seed, benchFrame, 0)
        : ((IsKeyDown(KEY_UP) ? 1.f : 0.f) + (IsKeyDown(KEY_DOWN) ? -1.f : 0.f));
      const float steer = benchOptions.enabled
        ? socketwire_examples::benchmark::deterministicAxis(benchOptions.seed, benchFrame, 1)
        : ((IsKeyDown(KEY_LEFT) ? -1.f : 0.f) + (IsKeyDown(KEY_RIGHT) ? 1.f : 0.f));

      for (Entity& e : entities)
      {
        if (e.eid == myEntity)
          send_entity_input(&connection, myEntity, thr, steer);
      }
    }
    const auto updateEnd = std::chrono::steady_clock::now();

    if (!benchOptions.enabled)
    {
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

  connection.disconnect();
  metrics.finish();
  socketwire_examples::benchmark::setActiveCollector(nullptr);
  if (!benchOptions.enabled)
    CloseWindow();
  return 0;
}
