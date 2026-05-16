#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <print>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

#include "benchmark_utils.hpp"
#include "entity.h"
#include "mathUtils.h"
#include "protocol.h"
#include "server_connection_hub.hpp"
#include "socketwire_example_utils.hpp"

static std::vector<Entity> entities;
static std::map<std::uint16_t,
                socketwire_examples::ServerConnectionHub::Client*>
  controlled_map;
static std::unordered_map<socketwire_examples::ServerConnectionHub::Client*,
                          std::uint32_t>
  cipher_keys;

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

static std::uint32_t RandomKey() {
  std::uniform_int_distribution<std::uint32_t> distribution;
  return distribution(RandomGenerator());
}

static std::uint16_t NextEntityId() {
  std::uint16_t max_eid = entities.empty() ? kInvalidEntity : entities[0].eid;
  for (const Entity& e : entities) max_eid = std::max(max_eid, e.eid);
  return static_cast<std::uint16_t>(max_eid + 1);
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
  const std::uint32_t color =
    0xff000000u + 0x00440000u * static_cast<std::uint32_t>(RandomInt(0, 4)) +
    0x00004400u * static_cast<std::uint32_t>(RandomInt(0, 4)) +
    0x00000044u * static_cast<std::uint32_t>(RandomInt(0, 4));

  Entity ent;
  ent.color = color;
  ent.x = static_cast<float>(RandomInt(0, 3)) * 2.f;
  ent.y = static_cast<float>(RandomInt(0, 3)) * 2.f;
  ent.speed = 0.f;
  ent.ori = RandomOrientation();
  ent.thr = 0.f;
  ent.steer = 0.f;
  ent.eid = new_eid;
  entities.push_back(ent);

  controlled_map[new_eid] = &client;
  BroadcastEntity(hub, ent);
  SendSetControlledEntity(client.connection.get(), new_eid);

  const std::uint32_t key = RandomKey();
  cipher_keys[&client] = key;
  SendCipherKey(client.connection.get(), key);
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

int main(int argc, const char** argv) {
  auto bench_options =
    socketwire_examples::benchmark::ParseOptions(argc, argv, 10131);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "cipher-ships", "socketwire", "server");
  socketwire_examples::benchmark::SetActiveCollector(&metrics);

  const std::uint16_t listen_port =
    bench_options.enabled
      ? bench_options.port
      : socketwire_examples::PortFromArgsOrEnv(
          argc, argv, 1, "SOCKETWIRE_CIPHER_SHIPS_PORT", 10131);

  auto socket = socketwire_examples::CreateUdpSocket(listen_port);
  if (socket == nullptr) return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);

  hub.SetConnectedCallback([](auto& client) {
    cipher_keys[&client] = 0;
    std::println("Connection with {:d}:{:d} established",
                 client.address.ipv4.hostOrderAddress, client.port);
  });

  hub.SetDisconnectedCallback([](auto& client) {
    std::println("Disconnected {:d}:{:d}", client.address.ipv4.hostOrderAddress,
                 client.port);
    cipher_keys.erase(&client);
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
        case kEClientToServerInput: {
          const auto key_it = cipher_keys.find(&client);
          const std::uint32_t key =
            key_it == cipher_keys.end() ? 0 : key_it->second;
          const std::vector<std::uint8_t> deciphered =
            DecipherData(data, size, key);
          OnInput(deciphered.data(), deciphered.size());
          break;
        }
        case kEServerToClientNewEntity:
        case kEServerToClientSetControlledEntity:
        case kEServerToClientSnapshot:
        case kEServerToClientKey:
          break;
      }
    });

  auto last_time = std::chrono::steady_clock::now();
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

    for (Entity& e : entities) {
      SimulateEntity(e, dt);
      for (auto* client : hub.Clients()) {
        if (client != nullptr && client->connection != nullptr &&
            client->connection->IsConnected()) {
          SendSnapshot(client->connection.get(), e.eid, e.x, e.y, e.ori);
        }
      }
    }
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
