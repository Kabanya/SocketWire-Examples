#include "server_connection_hub.hpp"
#include "benchmark_utils.hpp"
#include "socketwire_example_utils.hpp"

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
  const std::size_t bytes = text.size() + 1;
  if (client.connection->SendReliable(0, text.c_str(), bytes))
    socketwire_examples::benchmark::recordPayloadTx(bytes);
}

int main(int argc, const char** argv)
{
  auto benchOptions = socketwire_examples::benchmark::parseOptions(argc, argv, 0, 10887, 10888);
  socketwire_examples::benchmark::MetricsCollector metrics(
    benchOptions, "lobby-dots", "socketwire", "lobby");
  socketwire_examples::benchmark::setActiveCollector(&metrics);

  const std::uint16_t listenPort = benchOptions.enabled
    ? benchOptions.lobbyPort
    : socketwire_examples::portFromArgsOrEnv(argc, argv, 3, "SOCKETWIRE_LOBBY_DOTS_LOBBY_PORT", 10887);

  auto socket = socketwire_examples::createUdpSocket(listenPort);
  if (socket == nullptr)
    return 1;

  GameServerInfo gameServer;
  if (benchOptions.enabled)
  {
    gameServer.host = benchOptions.host;
    gameServer.port = benchOptions.gamePort;
  }
  else
  {
    gameServer.port = socketwire_examples::portFromArgsOrEnv(
      argc, argv, 2, "SOCKETWIRE_LOBBY_DOTS_GAME_PORT", static_cast<std::uint16_t>(gameServer.port));
  }
  if (!benchOptions.enabled && argc > 2)
  {
    gameServer.host = argv[1];
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
    socketwire_examples::benchmark::recordPayloadRx(size);
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
        if (peer != nullptr && peer->connection != nullptr && peer->connection->IsConnected())
          send_text(*peer, gameServerMsg);

      std::printf("Sent game server info to all connected players: %s\n", gameServerMsg.c_str());
    }
  });

  std::printf("Lobby server started on port %u\n", static_cast<unsigned>(listenPort));
  std::printf("Game server will be at %s:%d\n", gameServer.host.c_str(), gameServer.port);

  while (true)
  {
    if (benchOptions.enabled && metrics.done())
      break;
    const auto frameStart = std::chrono::steady_clock::now();
    const auto updateStart = frameStart;
    hub.poll();
    hub.update();
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
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  metrics.finish();
  socketwire_examples::benchmark::setActiveCollector(nullptr);
}
