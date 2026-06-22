#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <print>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "benchmark_utils.hpp"
#include "raylib.h"
#include "server_connection_hub.hpp"
#include "socketwire_example_utils.hpp"

struct Player {
  int id = -1;
  float x = 0.f;
  float y = 0.f;
  int ping = 0;
  socketwire_examples::ServerConnectionHub::Client* client = nullptr;
  std::string name;
};

static int GeneratePlayerId() {
  static int next_id = 1;
  return next_id++;
}

static void SendText(socketwire_examples::ServerConnectionHub::Client& client,
                     const std::string& text, bool reliable = false) {
  const std::size_t bytes = text.size() + 1;
  const bool sent =
    reliable ? client.connection->SendReliable(0, text.c_str(), bytes)
             : client.connection->SendUnsequenced(0, text.c_str(), bytes);
  if (sent) socketwire_examples::benchmark::RecordPayloadTx(bytes);
}

static void SendPlayerList(
  const std::vector<Player>& players,
  socketwire_examples::ServerConnectionHub::Client& target_client) {
  std::stringstream ss;
  ss << "PLAYERS ";
  for (const auto& player : players) {
    ss << player.id << " " << player.x << " " << player.y << " " << player.ping
       << ";";
  }

  SendText(target_client, ss.str());
}

static void BroadcastPlayerList(const std::vector<Player>& players) {
  for (const auto& player : players) {
    if (player.client != nullptr) SendPlayerList(players, *player.client);
  }
}

static void BroadcastPositions(const std::vector<Player>& players) {
  for (const auto& sender : players) {
    const std::string pos_str = "POS " + std::to_string(sender.id) + " " +
                                std::to_string(sender.x) + " " +
                                std::to_string(sender.y);

    for (const auto& receiver : players) {
      if (receiver.id != sender.id && receiver.client != nullptr) {
        SendText(*receiver.client, pos_str, false);
      }
    }
  }
}

static void BroadcastPings(const std::vector<Player>& players) {
  for (const auto& player : players) {
    if (player.client == nullptr) continue;

    std::stringstream ss;
    ss << "PINGS ";
    for (const auto& p : players) ss << p.id << " " << p.ping << ";";

    SendText(*player.client, ss.str());
  }
}

int main(int argc, const char** argv) {
  auto bench_options =
    socketwire_examples::benchmark::ParseOptions(argc, argv, 0, 10887, 10888);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "lobby-dots", "socketwire", "game-server");
  socketwire_examples::benchmark::SetActiveCollector(&metrics);

  const bool has_game_port_option =
    socketwire_examples::HasCommandLineOption(argc, argv, "--game-port");
  const int game_port_arg_index =
    argc > 1 && !socketwire_examples::IsCommandLineOption(argv[1]) ? 1 : 0;
  const std::uint16_t listen_port =
    bench_options.enabled || has_game_port_option
      ? bench_options.gamePort
      : socketwire_examples::PortFromArgsOrEnv(
          argc, argv, game_port_arg_index, "SOCKETWIRE_LOBBY_DOTS_GAME_PORT",
          10888);

  auto socket = socketwire_examples::CreateUdpSocket(listen_port);
  if (socket == nullptr) return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire_examples::ServerConnectionHub hub(socket.get(), cfg);

  std::vector<Player> players;

  hub.SetConnectedCallback([&](auto& client) {
    std::println("Player connected from {:d}:{:d}",
                 client.address.ipv4.hostOrderAddress, client.port);

    Player new_player;
    new_player.id = GeneratePlayerId();
    new_player.name = "Player_" + std::to_string(new_player.id);
    new_player.x = static_cast<float>(GetRandomValue(100, 500));
    new_player.y = static_cast<float>(GetRandomValue(100, 300));
    new_player.ping = static_cast<int>(client.connection->GetRtt());
    new_player.client = &client;

    const std::string welcome_msg =
      "WELCOME " + std::to_string(new_player.id) + " " + new_player.name;
    players.push_back(new_player);
    SendText(client, welcome_msg);
    BroadcastPlayerList(players);
  });

  hub.SetDisconnectedCallback([&](auto& client) {
    std::println("Player disconnected from {:d}:{:d}",
                 client.address.ipv4.hostOrderAddress, client.port);

    const auto it = std::find_if(
      players.begin(), players.end(),
      [&](const Player& player) { return player.client == &client; });
    if (it == players.end()) return;

    players.erase(it);

    BroadcastPlayerList(players);
  });

  hub.SetPacketCallback([&](auto& client, std::uint8_t, const void* data,
                            std::size_t size, bool) {
    socketwire_examples::benchmark::RecordPayloadRx(size);
    const auto it = std::find_if(
      players.begin(), players.end(),
      [&](const Player& player) { return player.client == &client; });
    if (it == players.end()) return;

    it->ping = static_cast<int>(client.connection->GetRtt());
    const std::string msg = socketwire_examples::ReadStringPayload(data, size);

    if (msg.starts_with("POS")) {
      std::istringstream ss(msg.substr(4));
      float x = 0.f;
      float y = 0.f;
      if (ss >> x >> y) {
        it->x = x;
        it->y = y;
      }
    }
  });

  auto last_broadcast_time = std::chrono::steady_clock::now();
  auto last_ping_time = std::chrono::steady_clock::now();

  std::println("Game server started on port {}",
               static_cast<unsigned>(listen_port));
  while (true) {
    if (bench_options.enabled && metrics.Done()) break;
    const auto frame_start = std::chrono::steady_clock::now();
    const auto update_start = frame_start;
    hub.Poll();
    hub.Update();

    const auto current_time = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
          current_time - last_broadcast_time)
          .count() > 50) {
      last_broadcast_time = current_time;
      if (!players.empty()) BroadcastPositions(players);
    }

    if (std::chrono::duration_cast<std::chrono::milliseconds>(current_time -
                                                              last_ping_time)
          .count() > 500) {
      last_ping_time = current_time;
      if (!players.empty()) {
        BroadcastPlayerList(players);
        BroadcastPings(players);
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

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  metrics.Finish();
  socketwire_examples::benchmark::SetActiveCollector(nullptr);
}
