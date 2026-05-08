#include "entity.h"
#include "protocol.h"

#include "server_connection_hub.hpp"
#include "socketwire_example_utils.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <random>
#include <thread>
#include <vector>

static std::vector<Entity> entities;
static std::map<std::uint16_t, socketwire_examples::ServerConnectionHub::Client*> controlledMap;

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

static std::uint16_t next_entity_id()
{
  std::uint16_t maxEid = entities.empty() ? INVALID_ENTITY : entities[0].eid;
  for (const Entity& e : entities)
    maxEid = std::max(maxEid, e.eid);
  return static_cast<std::uint16_t>(maxEid + 1);
}

static Entity make_ship(std::uint16_t eid, bool serverControlled)
{
  const std::uint32_t color = 0xff000000u +
                              0x00440000u * static_cast<std::uint32_t>(random_int(0, 4)) +
                              0x00004400u * static_cast<std::uint32_t>(random_int(0, 4)) +
                              0x00000044u * static_cast<std::uint32_t>(random_int(0, 4));

  Entity ent;
  ent.color = color;
  ent.serverControlled = serverControlled;
  ent.x = serverControlled
    ? static_cast<float>(random_int(0, static_cast<int>(WORLD_SIZE * 2.f) - 1)) - WORLD_SIZE
    : static_cast<float>(random_int(0, 3)) * 5.f;
  ent.y = serverControlled
    ? static_cast<float>(random_int(0, static_cast<int>(WORLD_SIZE * 2.f) - 1)) - WORLD_SIZE
    : static_cast<float>(random_int(0, 3)) * 5.f;
  ent.vx = 0.f;
  ent.vy = 0.f;
  ent.ori = random_orientation();
  ent.omega = 0.f;
  ent.thr = 0.f;
  ent.steer = 0.f;
  ent.eid = eid;
  return ent;
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
  Entity ent = make_ship(newEid, false);
  entities.push_back(ent);
  controlledMap[newEid] = &client;

  broadcast_entity(hub, ent);
  send_set_controlled_entity(client.connection.get(), newEid);
}

static void create_server_entity(socketwire_examples::ServerConnectionHub& hub)
{
  const std::uint16_t newEid = next_entity_id();
  Entity ent = make_ship(newEid, true);
  entities.push_back(ent);
  controlledMap[newEid] = nullptr;
  broadcast_entity(hub, ent);
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

static void update_ai(Entity& e)
{
  if (random_int(0, 99) == 0)
    e.thr = e.thr > 0.f ? 0.f : 1.f;
  if (random_int(0, 9) == 0)
    e.steer = e.steer != 0.f ? 0.f : static_cast<float>(random_int(0, 1) * 2 - 1);
}

static void simulate_world(socketwire_examples::ServerConnectionHub& hub, float dt)
{
  for (Entity& e : entities)
  {
    if (e.serverControlled)
      update_ai(e);

    simulate_entity(e, dt);

    for (auto* client : hub.clients())
      if (client != nullptr && client->connection != nullptr && client->connection->isConnected())
        send_snapshot(client->connection.get(), e.eid, e.x, e.y, e.ori);
  }
}

static void update_time(socketwire_examples::ServerConnectionHub& hub, std::uint32_t curTime)
{
  for (auto* client : hub.clients())
    if (client != nullptr && client->connection != nullptr && client->connection->isConnected())
      send_time_msec(client->connection.get(), curTime);
}

int main()
{
  auto socket = socketwire_examples::createUdpSocket(10131);
  if (socket == nullptr)
    return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);

  hub.setDisconnectedCallback([](auto& client)
  {
    for (auto& entry : controlledMap)
      if (entry.second == &client)
        entry.second = nullptr;
  });

  hub.setPacketCallback([&](auto& client, std::uint8_t, const void* data, std::size_t size, bool)
  {
    switch (get_packet_type(data, size))
    {
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

  constexpr std::size_t NUM_SHIPS = 100;
  for (std::size_t i = 0; i < NUM_SHIPS; ++i)
    create_server_entity(hub);

  const auto start = std::chrono::steady_clock::now();
  auto lastTime = start;
  while (true)
  {
    const auto curTime = std::chrono::steady_clock::now();
    const float dt =
      std::chrono::duration_cast<std::chrono::milliseconds>(curTime - lastTime).count() * 0.001f;
    lastTime = curTime;

    hub.poll();
    hub.update();
    simulate_world(hub, dt);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(curTime - start).count();
    update_time(hub, static_cast<std::uint32_t>(elapsedMs));

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}
