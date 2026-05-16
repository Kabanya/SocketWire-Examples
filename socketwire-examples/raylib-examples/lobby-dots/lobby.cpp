#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <print>
#include <string>
#include <thread>
#include <vector>

#include "benchmark_utils.hpp"
#include "server_connection_hub.hpp"
#include "socketwire_example_utils.hpp"

struct GameServerInfo {
  int port = 10888;
  std::string host = "localhost";
  bool sessionStarted = false;
};

static void SendText(socketwire_examples::ServerConnectionHub::Client& client,
                     const std::string& text) {
  const std::size_t bytes = text.size() + 1;
  if (client.connection->SendReliable(0, text.c_str(), bytes)) {
    socketwire_examples::benchmark::RecordPayloadTx(bytes);
  }
}

int main(int argc, const char** argv) {
  auto bench_options =
    socketwire_examples::benchmark::ParseOptions(argc, argv, 0, 10887, 10888);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "lobby-dots", "socketwire", "lobby");
  socketwire_examples::benchmark::SetActiveCollector(&metrics);

  const std::uint16_t listen_port =
    bench_options.enabled
      ? bench_options.lobbyPort
      : socketwire_examples::PortFromArgsOrEnv(
          argc, argv, 3, "SOCKETWIRE_LOBBY_DOTS_LOBBY_PORT", 10887);

  auto socket = socketwire_examples::CreateUdpSocket(listen_port);
  if (socket == nullptr) return 1;

  GameServerInfo gameServer;
  if (bench_options.enabled) {
    gameServer.host = bench_options.host;
    gameServer.port = bench_options.gamePort;
  } else {
    gameServer.port = socketwire_examples::PortFromArgsOrEnv(
      argc, argv, 2, "SOCKETWIRE_LOBBY_DOTS_GAME_PORT",
      static_cast<std::uint16_t>(gameServer.port));
  }
  if (!bench_options.enabled && argc > 2) {
    gameServer.host = argv[1];
  }

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);
  std::vector<socketwire_examples::ServerConnectionHub::Client*>
    connected_clients;

  hub.SetConnectedCallback([&](auto& client) {
    std::println("Client connected from {:d}:{:d}",
                 client.address.ipv4.hostOrderAddress, client.port);
    connected_clients.push_back(&client);

    if (gameServer.sessionStarted) {
      const std::string gameServerMsg =
        "GAMESERVER " + gameServer.host + " " + std::to_string(gameServer.port);
      SendText(client, gameServerMsg);
      std::println("Sent game server info to new player: {}", gameServerMsg);
    }
  });

  hub.SetDisconnectedCallback([&](auto& client) {
    std::println("Client disconnected from {:d}:{:d}",
                 client.address.ipv4.hostOrderAddress, client.port);
    std::erase(connected_clients, &client);
  });

  hub.SetPacketCallback([&](auto& client, std::uint8_t, const void* data,
                            std::size_t size, bool) {
    socketwire_examples::benchmark::RecordPayloadRx(size);
    const std::string packet =
      socketwire_examples::ReadStringPayload(data, size);
    std::println("Packet received from {:d}:{:d}: '{}'",
                 client.address.ipv4.hostOrderAddress, client.port, packet);

    if (!gameServer.sessionStarted && packet == "Start!") {
      std::println("Game session start requested!");
      gameServer.sessionStarted = true;
      const std::string gameServerMsg =
        "GAMESERVER " + gameServer.host + " " + std::to_string(gameServer.port);

      for (auto* peer : connected_clients) {
        if (peer != nullptr && peer->connection != nullptr &&
            peer->connection->IsConnected()) {
          SendText(*peer, gameServerMsg);
        }
      }

      std::println("Sent game server info to all connected players: {}",
                   gameServerMsg);
    }
  });

  std::println("Lobby server started on port {}",
               static_cast<unsigned>(listen_port));
  std::println("Game server will be at {}:{}", gameServer.host,
               gameServer.port);

  while (true) {
    if (bench_options.enabled && metrics.Done()) break;
    const auto frame_start = std::chrono::steady_clock::now();
    const auto update_start = frame_start;
    hub.Poll();
    hub.Update();
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
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  metrics.Finish();
  socketwire_examples::benchmark::SetActiveCollector(nullptr);
}
