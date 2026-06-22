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
  std::string host = "127.0.0.1";
  bool sessionStarted = false;
};

static void SendText(socketwire_examples::ServerConnectionHub::Client& client,
                     const std::string& text) {
  const std::size_t bytes = text.size() + 1;
  if (client.connection->SendUnsequenced(0, text.c_str(), bytes)) {
    socketwire_examples::benchmark::RecordPayloadTx(bytes);
  }
}

int main(int argc, const char** argv) {
  auto bench_options =
    socketwire_examples::benchmark::ParseOptions(argc, argv, 0, 10887, 10888);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "lobby-dots", "socketwire", "lobby");
  socketwire_examples::benchmark::SetActiveCollector(&metrics);

  const bool has_host_option =
    socketwire_examples::HasCommandLineOption(argc, argv, "--host");
  const bool has_lobby_port_option =
    socketwire_examples::HasCommandLineOption(argc, argv, "--lobby-port");
  const bool has_game_port_option =
    socketwire_examples::HasCommandLineOption(argc, argv, "--game-port");
  const bool has_positional_game_host =
    argc > 1 && argv[1] != nullptr &&
    !socketwire_examples::IsCommandLineOption(argv[1]) &&
    !socketwire_examples::ParsePort(argv[1]).has_value();
  const int game_port_arg_index = has_positional_game_host ? 2 : 0;
  const int lobby_port_arg_index = has_positional_game_host ? 3 : 0;

  const std::uint16_t listen_port =
    bench_options.enabled || has_lobby_port_option
      ? bench_options.lobbyPort
      : socketwire_examples::PortFromArgsOrEnv(
          argc, argv, lobby_port_arg_index, "SOCKETWIRE_LOBBY_DOTS_LOBBY_PORT",
          10887);

  auto socket = socketwire_examples::CreateUdpSocket(listen_port);
  if (socket == nullptr) return 1;

  GameServerInfo game_server;
  if (bench_options.enabled || has_host_option) {
    game_server.host = bench_options.host;
  } else if (has_positional_game_host) {
    game_server.host = argv[1];
  }

  if (bench_options.enabled || has_game_port_option) {
    game_server.port = bench_options.gamePort;
  } else {
    game_server.port = socketwire_examples::PortFromArgsOrEnv(
      argc, argv, game_port_arg_index, "SOCKETWIRE_LOBBY_DOTS_GAME_PORT",
      static_cast<std::uint16_t>(game_server.port));
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

    if (game_server.sessionStarted) {
      const std::string game_server_msg = "GAMESERVER " + game_server.host +
                                          " " +
                                          std::to_string(game_server.port);
      SendText(client, game_server_msg);
      std::println("Sent game server info to new player: {}", game_server_msg);
    }
  });

  hub.SetDisconnectedCallback([&](auto& client) {
    std::println("Client disconnected from {:d}:{:d}",
                 client.address.ipv4.hostOrderAddress, client.port);
    std::erase(connected_clients, &client);
  });

  hub.SetPacketCallback(
    [&](auto& client, std::uint8_t, const void* data, std::size_t size, bool) {
      socketwire_examples::benchmark::RecordPayloadRx(size);
      const std::string packet =
        socketwire_examples::ReadStringPayload(data, size);
      std::println("Packet received from {:d}:{:d}: '{}'",
                   client.address.ipv4.hostOrderAddress, client.port, packet);

      if (!game_server.sessionStarted && packet == "Start!") {
        std::println("Game session start requested!");
        game_server.sessionStarted = true;
        const std::string game_server_msg = "GAMESERVER " + game_server.host +
                                            " " +
                                            std::to_string(game_server.port);

        for (auto* peer : connected_clients) {
          if (peer != nullptr && peer->connection != nullptr &&
              peer->connection->IsConnected()) {
            SendText(*peer, game_server_msg);
          }
        }

        std::println("Sent game server info to all connected players: {}",
                     game_server_msg);
      }
    });

  std::println("Lobby server started on port {}",
               static_cast<unsigned>(listen_port));
  std::println("Game server will be at {}:{}", game_server.host,
               game_server.port);

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
