#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <random>
#include <thread>
#include <vector>

#include "benchmark_utils.hpp"
#include "entity.h"
#include "protocol.h"
#include "server_connection_hub.hpp"
#include "socketwire_example_utils.hpp"

static std::vector<Entity> entities;
static std::map<std::uint16_t,
                socketwire_examples::ServerConnectionHub::Client*>
  controlled_map;

static std::mt19937& RandomGenerator() {
  static std::random_device random_device;
  static std::mt19937 generator(random_device());
  return generator;
}

static int RandomInt(int min, int max) {
  std::uniform_int_distribution<int> distribution(min, max);
  return distribution(RandomGenerator());
}

static float RandomOrientation() {
  std::uniform_real_distribution<float> distribution(0.f, 3.141592654f);
  return distribution(RandomGenerator());
}

static std::uint16_t NextEntityId() {
  std::uint16_t max_eid = entities.empty() ? kInvalidEntity : entities[0].eid;
  for (const Entity& e : entities) max_eid = std::max(max_eid, e.eid);
  return static_cast<std::uint16_t>(max_eid + 1);
}

static Entity MakeShip(std::uint16_t eid, bool server_controlled) {
  const std::uint32_t color =
    0xff000000u + 0x00440000u * static_cast<std::uint32_t>(RandomInt(0, 4)) +
    0x00004400u * static_cast<std::uint32_t>(RandomInt(0, 4)) +
    0x00000044u * static_cast<std::uint32_t>(RandomInt(0, 4));

  Entity ent;
  ent.color = color;
  ent.serverControlled = server_controlled;
  ent.x = server_controlled ? static_cast<float>(RandomInt(
                                0, static_cast<int>(kWorldSize * 2.f) - 1)) -
                                kWorldSize
                            : static_cast<float>(RandomInt(0, 3)) * 5.f;
  ent.y = server_controlled ? static_cast<float>(RandomInt(
                                0, static_cast<int>(kWorldSize * 2.f) - 1)) -
                                kWorldSize
                            : static_cast<float>(RandomInt(0, 3)) * 5.f;
  ent.vx = 0.f;
  ent.vy = 0.f;
  ent.ori = RandomOrientation();
  ent.omega = 0.f;
  ent.thr = 0.f;
  ent.steer = 0.f;
  ent.eid = eid;
  return ent;
}

static void BroadcastEntity(socketwire_examples::ServerConnectionHub& hub,
                            const Entity& ent) {
  for (auto* client : hub.Clients()) {
    if (client != nullptr && client->connection != nullptr &&
        client->connection->IsConnected()) {
      SendNewEntity(client->connection.get(), ent);
    }
  }
}

static void OnJoin(socketwire_examples::ServerConnectionHub& hub,
                   socketwire_examples::ServerConnectionHub::Client& client) {
  for (const Entity& ent : entities) {
    SendNewEntity(client.connection.get(), ent);
  }

  const std::uint16_t new_eid = NextEntityId();
  Entity const ent = MakeShip(new_eid, false);
  entities.push_back(ent);
  controlled_map[new_eid] = &client;

  BroadcastEntity(hub, ent);
  SendSetControlledEntity(client.connection.get(), new_eid);
}

static void CreateServerEntity(socketwire_examples::ServerConnectionHub& hub) {
  const std::uint16_t new_eid = NextEntityId();
  Entity const ent = MakeShip(new_eid, true);
  entities.push_back(ent);
  controlled_map[new_eid] = nullptr;
  BroadcastEntity(hub, ent);
}

static void OnInput(const void* data, std::size_t size) {
  std::uint16_t eid = kInvalidEntity;
  float thr = 0.f;
  float steer = 0.f;
  DeserializeEntityInput(data, size, eid, thr, steer);

  for (Entity& e : entities) {
    if (e.eid == eid) {
      e.thr = thr;
      e.steer = steer;
      return;
    }
  }
}

static void UpdateAi(Entity& e) {
  if (RandomInt(0, 99) == 0) e.thr = e.thr > 0.f ? 0.f : 1.f;
  if (RandomInt(0, 9) == 0) {
    e.steer =
      e.steer != 0.f ? 0.f : static_cast<float>(RandomInt(0, 1) * 2 - 1);
  }
}

static void SimulateWorld(socketwire_examples::ServerConnectionHub& hub,
                          float dt) {
  for (Entity& e : entities) {
    if (e.serverControlled) UpdateAi(e);

    SimulateEntity(e, dt);

    for (auto* client : hub.Clients()) {
      if (client != nullptr && client->connection != nullptr &&
          client->connection->IsConnected()) {
        SendSnapshot(client->connection.get(), e.eid, e.x, e.y, e.ori);
      }
    }
  }
}

static void UpdateTime(socketwire_examples::ServerConnectionHub& hub,
                       std::uint32_t cur_time) {
  for (auto* client : hub.Clients()) {
    if (client != nullptr && client->connection != nullptr &&
        client->connection->IsConnected()) {
      SendTimeMsec(client->connection.get(), cur_time);
    }
  }
}

int main(int argc, const char** argv) {
  auto bench_options =
    socketwire_examples::benchmark::ParseOptions(argc, argv, 10131);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "ship-swarm", "socketwire", "server");
  socketwire_examples::benchmark::SetActiveCollector(&metrics);

  const std::uint16_t listen_port =
    bench_options.enabled
      ? bench_options.port
      : socketwire_examples::PortFromArgsOrEnv(
          argc, argv, 1, "SOCKETWIRE_SHIP_SWARM_PORT", 10131);

  auto socket = socketwire_examples::CreateUdpSocket(listen_port);
  if (socket == nullptr) return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);

  hub.SetDisconnectedCallback([](auto& client) {
    for (auto& entry : controlled_map) {
      if (entry.second == &client) entry.second = nullptr;
    }
  });

  hub.SetPacketCallback(
    [&](auto& client, std::uint8_t, const void* data, std::size_t size, bool) {
      socketwire_examples::benchmark::RecordPayloadRx(size);
      switch (GetPacketType(data, size)) {
        case kEClientToServerJoin:
          OnJoin(hub, client);
          break;
        case kEClientToServerInput:
          OnInput(data, size);
          break;
        case kEServerToClientNewEntity:
        case kEServerToClientSetControlledEntity:
        case kEServerToClientSnapshot:
        case kEServerToClientTimeMsec:
          break;
      }
    });

  constexpr std::size_t num_ships = 100;
  for (std::size_t i = 0; i < num_ships; ++i) CreateServerEntity(hub);

  const auto start = std::chrono::steady_clock::now();
  auto last_time = start;
  while (true) {
    if (bench_options.enabled && metrics.Done()) break;
    const auto frame_start = std::chrono::steady_clock::now();
    const auto cur_time = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                       cur_time - last_time)
                       .count() *
                     0.001f;
    last_time = cur_time;

    const auto update_start = std::chrono::steady_clock::now();
    hub.Poll();
    hub.Update();
    SimulateWorld(hub, dt);
    const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(cur_time - start)
        .count();
    UpdateTime(hub, static_cast<std::uint32_t>(elapsed_ms));
    const auto update_end = std::chrono::steady_clock::now();

    if (bench_options.enabled) {
      const auto clients = hub.Clients();
      metrics.SetConnectedClients(static_cast<int>(clients.size()));
      metrics.SetNetworkStats(
        socketwire_examples::benchmark::StatsFromClients(clients));
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
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  metrics.Finish();
  socketwire_examples::benchmark::SetActiveCollector(nullptr);
}
