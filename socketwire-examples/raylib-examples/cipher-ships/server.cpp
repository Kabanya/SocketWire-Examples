#include "entity.h"
#include "mathUtils.h"
#include "protocol.h"

#include "benchmark_utils.hpp"
#include "server_connection_hub.hpp"
#include "socketwire_example_utils.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

static std::vector<Entity> entities;
static std::map<std::uint16_t, socketwire_examples::ServerConnectionHub::Client*> controlledMap;
static std::unordered_map<socketwire_examples::ServerConnectionHub::Client*, std::uint32_t> cipherKeys;

static std::mt19937& random_generator()
{
  static std::random_device random_device;
  static std::mt19937 generator(random_device());
  return generator;
}

static int random_int(int min, int max)
{
  std::uniform_int_distribution<int> distribution(min, max);
  return distribution(random_generator());
}

static float random_orientation()
{
  std::uniform_real_distribution<float> distribution(0.f, 3.141592654f);
  return distribution(random_generator());
}

static std::uint32_t random_key()
{
  std::uniform_int_distribution<std::uint32_t> distribution;
  return distribution(random_generator());
}

static std::uint16_t next_entity_id()
{
  std::uint16_t maxEid = entities.empty() ? INVALID_ENTITY : entities[0].eid;
  for (const Entity& e : entities)
    maxEid = std::max(maxEid, e.eid);
  return static_cast<std::uint16_t>(maxEid + 1);
}

static void broadcast_entity(socketwire_examples::ServerConnectionHub& hub, const Entity& ent)
{
  for (auto* client : hub.clients())
    if (client != nullptr && client->connection != nullptr && client->connection->isConnected())
      send_new_entity(client->connection.get(), ent);
}

static void on_join(socketwire_examples::ServerConnectionHub& hub,
                    socketwire_examples::ServerConnectionHub::Client& client)
{
  for (const Entity& ent : entities)
    send_new_entity(client.connection.get(), ent);

  const std::uint16_t newEid = next_entity_id();
  const std::uint32_t color = 0xff000000u +
                              0x00440000u * static_cast<std::uint32_t>(random_int(0, 4)) +
                              0x00004400u * static_cast<std::uint32_t>(random_int(0, 4)) +
                              0x00000044u * static_cast<std::uint32_t>(random_int(0, 4));

  Entity ent;
  ent.color = color;
  ent.x = static_cast<float>(random_int(0, 3)) * 2.f;
  ent.y = static_cast<float>(random_int(0, 3)) * 2.f;
  ent.speed = 0.f;
  ent.ori = random_orientation();
  ent.thr = 0.f;
  ent.steer = 0.f;
  ent.eid = newEid;
  entities.push_back(ent);

  controlledMap[newEid] = &client;
  broadcast_entity(hub, ent);
  send_set_controlled_entity(client.connection.get(), newEid);

  const std::uint32_t key = random_key();
  cipherKeys[&client] = key;
  send_cipher_key(client.connection.get(), key);
}

static void on_input(const void* data, std::size_t size)
{
  std::uint16_t eid = INVALID_ENTITY;
  float thr = 0.f;
  float steer = 0.f;
  deserialize_entity_input(data, size, eid, thr, steer);

  for (Entity& e : entities)
  {
    if (e.eid == eid)
    {
      e.thr = thr;
      e.steer = steer;
      return;
    }
  }
}

int main(int argc, const char** argv)
{
  auto benchOptions = socketwire_examples::benchmark::parseOptions(argc, argv, 10131);
  socketwire_examples::benchmark::MetricsCollector metrics(
    benchOptions, "cipher-ships", "socketwire", "server");
  socketwire_examples::benchmark::setActiveCollector(&metrics);

  const std::uint16_t listenPort = benchOptions.enabled
    ? benchOptions.port
    : socketwire_examples::portFromArgsOrEnv(argc, argv, 1, "SOCKETWIRE_CIPHER_SHIPS_PORT", 10131);

  auto socket = socketwire_examples::createUdpSocket(listenPort);
  if (socket == nullptr)
    return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);

  hub.setConnectedCallback([](auto& client)
  {
    cipherKeys[&client] = 0;
    std::printf("Connection with %u:%u established\n", client.address.ipv4.hostOrderAddress, client.port);
  });

  hub.setDisconnectedCallback([](auto& client)
  {
    std::printf("Disconnected %u:%u\n", client.address.ipv4.hostOrderAddress, client.port);
    cipherKeys.erase(&client);
    for (auto& entry : controlledMap)
      if (entry.second == &client)
        entry.second = nullptr;
  });

  hub.setPacketCallback([&](auto& client, std::uint8_t, const void* data, std::size_t size, bool)
  {
    socketwire_examples::benchmark::recordPayloadRx(size);
    switch (get_packet_type(data, size))
    {
      case E_CLIENT_TO_SERVER_JOIN:
        on_join(hub, client);
        break;
      case E_CLIENT_TO_SERVER_INPUT:
      {
        const auto keyIt = cipherKeys.find(&client);
        const std::uint32_t key = keyIt == cipherKeys.end() ? 0 : keyIt->second;
        const std::vector<std::uint8_t> deciphered = decipher_data(data, size, key);
        on_input(deciphered.data(), deciphered.size());
        break;
      }
      case E_SERVER_TO_CLIENT_NEW_ENTITY:
      case E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY:
      case E_SERVER_TO_CLIENT_SNAPSHOT:
      case E_SERVER_TO_CLIENT_KEY:
        break;
    }
  });

  auto lastTime = std::chrono::steady_clock::now();
  while (true)
  {
    if (benchOptions.enabled && metrics.done())
      break;
    const auto frameStart = std::chrono::steady_clock::now();
    const auto curTime = std::chrono::steady_clock::now();
    const float dt =
      std::chrono::duration_cast<std::chrono::milliseconds>(curTime - lastTime).count() * 0.001f;
    lastTime = curTime;

    const auto updateStart = std::chrono::steady_clock::now();
    hub.poll();
    hub.update();

    for (Entity& e : entities)
    {
      simulate_entity(e, dt);
      for (auto* client : hub.clients())
        if (client != nullptr && client->connection != nullptr && client->connection->isConnected())
          send_snapshot(client->connection.get(), e.eid, e.x, e.y, e.ori);
    }
    const auto updateEnd = std::chrono::steady_clock::now();

    if (benchOptions.enabled)
    {
      const auto clients = hub.clients();
      metrics.setConnectedClients(static_cast<int>(clients.size()));
      metrics.setNetworkStats(socketwire_examples::benchmark::statsFromClients(clients));
      metrics.recordUpdateMs(static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(updateEnd - updateStart).count()) / 1000.0);
      metrics.recordFrameMs(static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - frameStart).count()) / 1000.0);
      metrics.maybeWriteSample();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  metrics.finish();
  socketwire_examples::benchmark::setActiveCollector(nullptr);
}
