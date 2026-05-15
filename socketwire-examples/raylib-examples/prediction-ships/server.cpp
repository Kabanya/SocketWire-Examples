#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <random>
#include <thread>
#include <vector>

#include "benchmark_utils.hpp"
#include "entity.h"
#include "mathUtils.h"
#include "protocol.h"
#include "server_connection_hub.hpp"
#include "socketwire_example_utils.hpp"

static std::uint32_t frameCounter = 0;
static std::vector<Entity> entities;
static std::map<std::uint16_t,
                socketwire_examples::ServerConnectionHub::Client*>
  controlledMap;

static std::mt19937& random_generator() {
  static std::random_device random_device;
  static std::mt19937 generator(random_device());
  return generator;
}

static int random_int(int min, int max) {
  std::uniform_int_distribution<int> distribution(min, max);
  return distribution(random_generator());
}

static float random_orientation() {
  std::uniform_real_distribution<float> distribution(0.f, 3.141592654f);
  return distribution(random_generator());
}

static std::uint16_t next_entity_id() {
  std::uint16_t maxEid = entities.empty() ? INVALID_ENTITY : entities[0].eid;
  for (const Entity& e : entities) maxEid = std::max(maxEid, e.eid);
  return static_cast<std::uint16_t>(maxEid + 1);
}

static void broadcast_entity(socketwire_examples::ServerConnectionHub& hub,
                             const Entity& ent) {
  for (auto* client : hub.clients())
    if (client != nullptr && client->connection != nullptr &&
        client->connection->IsConnected())
      send_new_entity(client->connection.get(), ent);
}

static void on_join(socketwire_examples::ServerConnectionHub& hub,
                    socketwire_examples::ServerConnectionHub::Client& client) {
  for (const Entity& ent : entities)
    send_new_entity(client.connection.get(), ent);

  const std::uint16_t newEid = next_entity_id();
  const std::uint32_t color =
    0xff000000u + 0x00440000u * static_cast<std::uint32_t>(random_int(0, 4)) +
    0x00004400u * static_cast<std::uint32_t>(random_int(0, 4)) +
    0x00000044u * static_cast<std::uint32_t>(random_int(0, 4));

  Entity ent;
  ent.color = color;
  ent.x = static_cast<float>(random_int(0, 3)) * 5.f;
  ent.y = static_cast<float>(random_int(0, 3)) * 5.f;
  ent.vx = 0.f;
  ent.vy = 0.f;
  ent.ori = random_orientation();
  ent.omega = 0.f;
  ent.thr = 0.f;
  ent.steer = 0.f;
  ent.eid = newEid;
  entities.push_back(ent);
  controlledMap[newEid] = &client;

  broadcast_entity(hub, ent);
  send_set_controlled_entity(client.connection.get(), newEid);
}

static void on_input(const void* data, std::size_t size) {
  std::uint16_t eid = INVALID_ENTITY;
  float thr = 0.f;
  float steer = 0.f;
  deserialize_entity_input(data, size, eid, thr, steer);

  for (Entity& e : entities) {
    if (e.eid == eid) {
      e.thr = thr;
      e.steer = steer;
      return;
    }
  }
}

static void simulate_world(socketwire_examples::ServerConnectionHub& hub,
                           float dt) {
  const TimePoint curTime = std::chrono::steady_clock::now();
  for (Entity& e : entities) {
    simulate_entity(e, dt);
    for (auto* client : hub.clients())
      if (client != nullptr && client->connection != nullptr &&
          client->connection->IsConnected())
        send_snapshot(client->connection.get(), e.eid, e.x, e.y, e.ori, e.vx,
                      e.vy, e.omega, curTime, frameCounter);
  }
}

static void update_time(socketwire_examples::ServerConnectionHub& hub,
                        std::uint32_t curTime) {
  for (auto* client : hub.clients())
    if (client != nullptr && client->connection != nullptr &&
        client->connection->IsConnected())
      send_time_msec(client->connection.get(), curTime);
}

int main(int argc, const char** argv) {
  auto benchOptions =
    socketwire_examples::benchmark::parseOptions(argc, argv, 10131);
  socketwire_examples::benchmark::MetricsCollector metrics(
    benchOptions, "prediction-ships", "socketwire", "server");
  socketwire_examples::benchmark::setActiveCollector(&metrics);

  const std::uint16_t listenPort =
    benchOptions.enabled
      ? benchOptions.port
      : socketwire_examples::portFromArgsOrEnv(
          argc, argv, 1, "SOCKETWIRE_PREDICTION_SHIPS_PORT", 10131);

  auto socket = socketwire_examples::createUdpSocket(listenPort);
  if (socket == nullptr) return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);

  hub.setDisconnectedCallback([](auto& client) {
    for (auto& entry : controlledMap)
      if (entry.second == &client) entry.second = nullptr;
  });

  hub.setPacketCallback(
    [&](auto& client, std::uint8_t, const void* data, std::size_t size, bool) {
      socketwire_examples::benchmark::recordPayloadRx(size);
      switch (get_packet_type(data, size)) {
        case E_CLIENT_TO_SERVER_JOIN:
          on_join(hub, client);
          break;
        case E_CLIENT_TO_SERVER_INPUT:
          on_input(data, size);
          break;
        case E_SERVER_TO_CLIENT_NEW_ENTITY:
        case E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY:
        case E_SERVER_TO_CLIENT_SNAPSHOT:
        case E_SERVER_TO_CLIENT_TIME_MSEC:
          break;
      }
    });

  const auto start = std::chrono::steady_clock::now();
  auto lastTime = start;
  float accumulatedTimeMs = 0.f;
  frameCounter = 0;

  while (true) {
    if (benchOptions.enabled && metrics.done()) break;
    const auto frameStart = std::chrono::steady_clock::now();
    const auto curTime = std::chrono::steady_clock::now();
    const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(curTime - lastTime)
        .count();
    lastTime = curTime;
    accumulatedTimeMs += static_cast<float>(elapsed);

    if (accumulatedTimeMs >= FIXED_DT * 1000.f) {
      const auto updateStart = std::chrono::steady_clock::now();
      simulate_world(hub, FIXED_DT);
      hub.poll();
      hub.update();
      const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(curTime - start)
          .count();
      update_time(hub, static_cast<std::uint32_t>(elapsedMs));

      ++frameCounter;
      accumulatedTimeMs -= FIXED_DT * 1000.f;
      const auto updateEnd = std::chrono::steady_clock::now();
      if (benchOptions.enabled) {
        const auto clients = hub.clients();
        metrics.setConnectedClients(static_cast<int>(clients.size()));
        metrics.setNetworkStats(
          socketwire_examples::benchmark::statsFromClients(clients));
        metrics.recordUpdateMs(
          static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(updateEnd -
                                                                  updateStart)
              .count()) /
          1000.0);
        metrics.recordFrameMs(
          static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - frameStart)
              .count()) /
          1000.0);
        metrics.maybeWriteSample();
      }
    }

    std::this_thread::sleep_for(std::chrono::microseconds(200000));
  }

  metrics.finish();
  socketwire_examples::benchmark::setActiveCollector(nullptr);
}
