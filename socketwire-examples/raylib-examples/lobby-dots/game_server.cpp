#include "raylib.h"

#include "server_connection_hub.hpp"
#include "socketwire_example_utils.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

struct Player
{
  int id = -1;
  float x = 0.f;
  float y = 0.f;
  int ping = 0;
  socketwire_examples::ServerConnectionHub::Client* client = nullptr;
  std::string name;
};

static int generatePlayerID()
{
  static int nextID = 1;
  return nextID++;
}

static void send_text(socketwire_examples::ServerConnectionHub::Client& client,
                      const std::string& text,
                      bool reliable = true)
{
  if (reliable)
    client.connection->sendReliable(0, text.c_str(), text.size() + 1);
  else
    client.connection->sendUnsequenced(0, text.c_str(), text.size() + 1);
}

static void sendPlayerList(const std::vector<Player>& players,
                           socketwire_examples::ServerConnectionHub::Client& targetClient)
{
  std::stringstream ss;
  ss << "PLAYERS ";
  for (const auto& player : players)
    ss << player.id << " " << player.x << " " << player.y << " " << player.ping << ";";

  send_text(targetClient, ss.str());
}

static void broadcastNewPlayer(const Player& newPlayer, const std::vector<Player>& players)
{
  const std::string msg = "NEWPLAYER " + std::to_string(newPlayer.id) + " " + newPlayer.name;
  for (const auto& player : players)
    if (player.id != newPlayer.id && player.client != nullptr)
      send_text(*player.client, msg);
}

static void broadcastPositions(const std::vector<Player>& players)
{
  for (const auto& sender : players)
  {
    const std::string posStr =
      "POS " + std::to_string(sender.id) + " " + std::to_string(sender.x) + " " + std::to_string(sender.y);

    for (const auto& receiver : players)
      if (receiver.id != sender.id && receiver.client != nullptr)
        send_text(*receiver.client, posStr, false);
  }
}

static void broadcastPings(const std::vector<Player>& players)
{
  for (const auto& player : players)
  {
    if (player.client == nullptr)
      continue;

    std::stringstream ss;
    ss << "PINGS ";
    for (const auto& p : players)
      ss << p.id << " " << p.ping << ";";

    send_text(*player.client, ss.str());
  }
}

int main(int argc, const char** argv)
{
  int port = 10888;
  if (argc > 1)
    port = std::atoi(argv[1]);

  auto socket = socketwire_examples::createUdpSocket(static_cast<std::uint16_t>(port));
  if (socket == nullptr)
    return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);

  std::vector<Player> players;

  hub.setConnectedCallback([&](auto& client)
  {
    std::printf("Player connected from %u:%u\n", client.address.ipv4.hostOrderAddress, client.port);

    Player newPlayer;
    newPlayer.id = generatePlayerID();
    newPlayer.name = "Player_" + std::to_string(newPlayer.id);
    newPlayer.x = static_cast<float>(GetRandomValue(100, 500));
    newPlayer.y = static_cast<float>(GetRandomValue(100, 300));
    newPlayer.ping = static_cast<int>(client.connection->getRTT());
    newPlayer.client = &client;

    const std::string welcomeMsg = "WELCOME " + std::to_string(newPlayer.id) + " " + newPlayer.name;
    send_text(client, welcomeMsg);
    sendPlayerList(players, client);

    players.push_back(newPlayer);
    broadcastNewPlayer(newPlayer, players);
  });

  hub.setDisconnectedCallback([&](auto& client)
  {
    std::printf("Player disconnected from %u:%u\n", client.address.ipv4.hostOrderAddress, client.port);

    const auto it = std::find_if(players.begin(), players.end(),
      [&](const Player& player) { return player.client == &client; });
    if (it == players.end())
      return;

    const int disconnectedID = it->id;
    players.erase(it);

    const std::string disconnectMsg = "PLAYERLEFT " + std::to_string(disconnectedID);
    for (const auto& player : players)
      if (player.client != nullptr)
        send_text(*player.client, disconnectMsg);
  });

  hub.setPacketCallback([&](auto& client, std::uint8_t, const void* data, std::size_t size, bool)
  {
    const auto it = std::find_if(players.begin(), players.end(),
      [&](const Player& player) { return player.client == &client; });
    if (it == players.end())
      return;

    it->ping = static_cast<int>(client.connection->getRTT());
    const std::string msg = socketwire_examples::readStringPayload(data, size);

    if (msg.substr(0, 3) == "POS")
    {
      std::istringstream ss(msg.substr(4));
      float x = 0.f;
      float y = 0.f;
      if (ss >> x >> y)
      {
        it->x = x;
        it->y = y;
      }
    }
  });

  auto lastBroadcastTime = std::chrono::steady_clock::now();
  auto lastPingTime = std::chrono::steady_clock::now();

  std::printf("Game server started on port %d\n", port);
  while (true)
  {
    hub.poll();
    hub.update();

    const auto currentTime = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastBroadcastTime).count() > 50)
    {
      lastBroadcastTime = currentTime;
      if (!players.empty())
        broadcastPositions(players);
    }

    if (std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastPingTime).count() > 500)
    {
      lastPingTime = currentTime;
      if (!players.empty())
        broadcastPings(players);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
