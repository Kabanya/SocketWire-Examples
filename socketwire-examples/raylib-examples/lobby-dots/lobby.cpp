#include "server_connection_hub.hpp"
#include "socketwire_example_utils.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

struct GameServerInfo
{
  int port = 10888;
  std::string host = "localhost";
  bool sessionStarted = false;
};

static void send_text(socketwire_examples::ServerConnectionHub::Client& client, const std::string& text)
{
  client.connection->sendReliable(0, text.c_str(), text.size() + 1);
}

int main(int argc, const char** argv)
{
  auto socket = socketwire_examples::createUdpSocket(10887);
  if (socket == nullptr)
    return 1;

  GameServerInfo gameServer;
  if (argc > 2)
  {
    gameServer.host = argv[1];
    gameServer.port = std::atoi(argv[2]);
  }

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  std::vector<socketwire_examples::ServerConnectionHub::Client*> connectedClients;

  hub.setConnectedCallback([&](auto& client)
  {
    std::printf("Client connected from %u:%u\n", client.address.ipv4.hostOrderAddress, client.port);
    connectedClients.push_back(&client);

    if (gameServer.sessionStarted)
    {
      const std::string gameServerMsg =
        "GAMESERVER " + gameServer.host + " " + std::to_string(gameServer.port);
      send_text(client, gameServerMsg);
      std::printf("Sent game server info to new player: %s\n", gameServerMsg.c_str());
    }
  });

  hub.setDisconnectedCallback([&](auto& client)
  {
    std::printf("Client disconnected from %u:%u\n", client.address.ipv4.hostOrderAddress, client.port);
    std::erase(connectedClients, &client);
  });

  hub.setPacketCallback([&](auto& client, std::uint8_t, const void* data, std::size_t size, bool)
  {
    const std::string packet = socketwire_examples::readStringPayload(data, size);
    std::printf("Packet received from %u:%u: '%s'\n",
                client.address.ipv4.hostOrderAddress,
                client.port,
                packet.c_str());

    if (!gameServer.sessionStarted && packet == "Start!")
    {
      std::printf("Game session start requested!\n");
      gameServer.sessionStarted = true;
      const std::string gameServerMsg =
        "GAMESERVER " + gameServer.host + " " + std::to_string(gameServer.port);

      for (auto* peer : connectedClients)
        if (peer != nullptr && peer->connection != nullptr && peer->connection->isConnected())
          send_text(*peer, gameServerMsg);

      std::printf("Sent game server info to all connected players: %s\n", gameServerMsg.c_str());
    }
  });

  std::printf("Lobby server started on port 10887\n");
  std::printf("Game server will be at %s:%d\n", gameServer.host.c_str(), gameServer.port);

  while (true)
  {
    hub.poll();
    hub.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
