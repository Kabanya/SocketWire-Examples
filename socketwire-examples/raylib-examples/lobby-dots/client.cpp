#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <print>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "benchmark_utils.hpp"
#include "raylib.h"
#include "reliable_connection.hpp"
#include "socketwire_example_utils.hpp"

struct Player {
  int id = -1;
  float x = 0.f;
  float y = 0.f;
  int ping = 0;
};

struct ClientState {
  bool connectedToLobby = false;
  bool connectedToGameServer = false;
  std::string gameServerStatus = "Connecting to lobby...";
  std::vector<Player> players;
  int myPlayerId = -1;
  std::string pendingGameHost;
  std::uint16_t pendingGamePort = 0;
};

static void SendText(socketwire::ReliableConnection& connection,
                     const std::string& text, bool reliable = true) {
  const std::size_t bytes = text.size() + 1;
  const bool sent = reliable
                      ? connection.SendReliable(0, text.c_str(), bytes)
                      : connection.SendUnsequenced(0, text.c_str(), bytes);
  if (sent) socketwire_examples::benchmark::RecordPayloadTx(bytes);
}

static void SendFragmentedPacket(socketwire::ReliableConnection& connection) {
  const char* base_msg = "Stay awhile and listen. ";
  const std::size_t msg_len = std::char_traits<char>::length(base_msg);

  constexpr std::size_t send_size = 2500;
  std::string huge_message(send_size, '\0');
  for (std::size_t i = 0; i < send_size - 1; ++i) {
    huge_message[i] = base_msg[i % msg_len];
  }

  if (connection.SendReliable(0, huge_message.c_str(), huge_message.size())) {
    socketwire_examples::benchmark::RecordPayloadTx(huge_message.size());
  }
}

static void SendMicroPacket(socketwire::ReliableConnection& connection) {
  SendText(connection, "dv/dt", false);
}

static void SendPosition(socketwire::ReliableConnection& connection, float x,
                         float y) {
  char pos_msg[64]{};
  std::snprintf(pos_msg, sizeof(pos_msg), "POS %.2f %.2f", x, y);
  SendText(connection, pos_msg, false);
}

enum class ConnectionTarget { kLobby, kGame };

class ClientHandler final : public socketwire::IReliableConnectionHandler {
 public:
  ClientHandler(ClientState& state, ConnectionTarget target)
      : state_(state), target_(target) {}

  void OnConnected() override {
    if (target_ == ConnectionTarget::kLobby) {
      state_.connectedToLobby = true;
      state_.gameServerStatus = "Connected to lobby";
    } else {
      state_.connectedToGameServer = true;
      state_.gameServerStatus = "Connected to game server";
    }
  }

  void OnDisconnected() override {
    if (target_ == ConnectionTarget::kLobby) {
      state_.connectedToLobby = false;
      state_.gameServerStatus = "Disconnected from lobby";
    } else {
      state_.connectedToGameServer = false;
      state_.gameServerStatus = "Disconnected from game server";
    }
  }

  void OnReliableReceived(std::uint8_t channel, const void* data,
                          std::size_t size) override {
    socketwire_examples::benchmark::RecordPayloadRx(size);
    HandlePacket(channel, data, size);
  }

  void OnUnreliableReceived(std::uint8_t channel, const void* data,
                            std::size_t size) override {
    socketwire_examples::benchmark::RecordPayloadRx(size);
    HandlePacket(channel, data, size);
  }

 private:
  ClientState& state_;
  ConnectionTarget target_;

  void HandlePacket([[maybe_unused]] std::uint8_t channel, const void* data,
                    std::size_t size) {
    const std::string text = socketwire_examples::ReadStringPayload(data, size);
    std::println("Packet received '{}'", text);

    if (target_ == ConnectionTarget::kLobby) {
      if (!state_.connectedToGameServer && text.starts_with("GAMESERVER")) {
        char server_ip[256]{};
        int server_port = 0;
        if (std::sscanf(text.c_str(), "GAMESERVER %255s %d", server_ip,
                        &server_port) == 2) {
          state_.pendingGameHost = server_ip;
          state_.pendingGamePort = static_cast<std::uint16_t>(server_port);
          state_.gameServerStatus = "Connecting to game server...";
        }
      }
      return;
    }

    if (text.starts_with("WELCOME")) {
      int id = -1;
      char name[256]{};
      if (std::sscanf(text.c_str(), "WELCOME %d %255s", &id, name) >= 1) {
        state_.myPlayerId = id;
        state_.gameServerStatus =
          "Playing as player " + std::to_string(state_.myPlayerId);
      }
    } else if (text.starts_with("PLAYERS")) {
      state_.players.clear();
      std::istringstream ss(text.substr(8));
      Player player;
      std::string token;

      while (std::getline(ss, token, ';')) {
        std::istringstream player_stream(token);
        if (player_stream >> player.id >> player.x >> player.y >> player.ping) {
          state_.players.push_back(player);
        }
      }
    } else if (text.starts_with("POS")) {
      int player_id = -1;
      float x = 0.f;
      float y = 0.f;
      if (std::sscanf(text.c_str(), "POS %d %f %f", &player_id, &x, &y) == 3) {
        for (auto& player : state_.players) {
          if (player.id == player_id) {
            player.x = x;
            player.y = y;
            break;
          }
        }
      }
    } else if (text.starts_with("NEWPLAYER")) {
      int player_id = -1;
      char player_name[256]{};
      if (std::sscanf(text.c_str(), "NEWPLAYER %d %255s", &player_id,
                      player_name) >= 1) {
        const auto exists = std::ranges::any_of(
          state_.players,
          [player_id](const Player& player) { return player.id == player_id; });

        if (!exists) state_.players.push_back(Player{player_id, 0.f, 0.f, 0});
      }
    } else if (text.starts_with("PLAYERLEFT")) {
      int player_id = -1;
      if (std::sscanf(text.c_str(), "PLAYERLEFT %d", &player_id) == 1) {
        std::erase_if(state_.players, [player_id](const Player& player) {
          return player.id == player_id;
        });
      }
    } else if (text.starts_with("PINGS")) {
      std::println("Processing ping data: {}", text);
      std::istringstream ss(text.substr(6));
      std::string token;
      while (std::getline(ss, token, ';')) {
        if (token.empty()) continue;

        int player_id = -1;
        int ping_value = 0;
        std::istringstream ping_stream(token);
        if (ping_stream >> player_id >> ping_value) {
          bool updated = false;
          for (auto& player : state_.players) {
            if (player.id == player_id) {
              player.ping = ping_value;
              updated = true;
              break;
            }
          }

          if (!updated && player_id != state_.myPlayerId) {
            state_.players.push_back(Player{player_id, 0.f, 0.f, ping_value});
          }
        }
      }
    }
  }
};

int main(int argc, const char** argv) {
  auto bench_options =
    socketwire_examples::benchmark::ParseOptions(argc, argv, 0, 10887, 10888);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "lobby-dots", "socketwire", "client");
  socketwire_examples::benchmark::SetActiveCollector(&metrics);

  const std::uint16_t connect_lobby_port =
    bench_options.enabled
      ? bench_options.lobbyPort
      : socketwire_examples::PortFromArgsOrEnv(
          argc, argv, 1, "SOCKETWIRE_LOBBY_DOTS_LOBBY_PORT", 10887);

  int width = 800;
  int height = 600;
  if (!bench_options.enabled) InitWindow(width, height, "Lobby Dots");

  if (!bench_options.enabled) {
    const int scr_width = GetMonitorWidth(0);
    const int scr_height = GetMonitorHeight(0);
    if (scr_width < width || scr_height < height) {
      width = std::min(scr_width, width);
      height = std::min(scr_height - 150, height);
      SetWindowSize(width, height);
    }
  }

  if (!bench_options.enabled) SetTargetFPS(60);

  auto lobby_socket = socketwire_examples::CreateUdpSocket(0);
  if (lobby_socket == nullptr) return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire::ReliableConnection lobby_connection(lobby_socket.get(), cfg);

  ClientState state;
  ClientHandler lobby_handler(state, ConnectionTarget::kLobby);
  lobby_connection.SetHandler(&lobby_handler);
  lobby_connection.Connect(
    socketwire_examples::ResolveAddress(bench_options.host),
    connect_lobby_port);

  std::unique_ptr<socketwire::ISocket> game_socket;
  std::unique_ptr<socketwire::ReliableConnection> game_connection;
  std::unique_ptr<ClientHandler> game_handler;

  auto last_fragmented_send_time = std::chrono::steady_clock::now();
  auto last_micro_send_time = last_fragmented_send_time;
  auto last_position_send_time = last_fragmented_send_time;

  auto posx = static_cast<float>(GetRandomValue(100, 500));
  auto posy = static_cast<float>(GetRandomValue(100, 500));
  float velx = 0.f;
  float vely = 0.f;
  bool start_sent = false;
  std::uint64_t bench_frame = 0;

  while (bench_options.enabled ? !metrics.Done() : !WindowShouldClose()) {
    const auto frame_start = std::chrono::steady_clock::now();
    const float dt = bench_options.enabled ? (1.f / 60.f) : GetFrameTime();

    const auto update_start = std::chrono::steady_clock::now();
    lobby_connection.Tick();
    if (game_connection != nullptr) game_connection->Tick();

    if (!state.pendingGameHost.empty() && game_connection == nullptr) {
      game_socket = socketwire_examples::CreateUdpSocket(0);
      if (game_socket != nullptr) {
        game_connection = std::make_unique<socketwire::ReliableConnection>(
          game_socket.get(), cfg);
        game_handler =
          std::make_unique<ClientHandler>(state, ConnectionTarget::kGame);
        game_connection->SetHandler(game_handler.get());
        game_connection->Connect(
          socketwire_examples::ResolveAddress(state.pendingGameHost),
          state.pendingGamePort);
      } else {
        state.gameServerStatus = "Cannot connect to game server";
      }
      state.pendingGameHost.clear();
    }

    const auto now = std::chrono::steady_clock::now();

    if (state.connectedToGameServer && game_connection != nullptr &&
        std::chrono::duration_cast<std::chrono::milliseconds>(
          now - last_position_send_time)
            .count() > 50) {
      last_position_send_time = now;
      SendPosition(*game_connection, posx, posy);
    }

    if (state.connectedToLobby) {
      if (std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_fragmented_send_time)
            .count() > 1000) {
        last_fragmented_send_time = now;
        // send_fragmented_packet(lobbyConnection);
        (void)SendFragmentedPacket;
      }
      if (std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_micro_send_time)
            .count() > 100) {
        last_micro_send_time = now;
        // send_micro_packet(lobbyConnection);
        (void)SendMicroPacket;
      }
    }

    if (!bench_options.enabled && IsKeyPressed(KEY_ESCAPE)) break;

    if (((bench_options.enabled && !start_sent) ||
         (!bench_options.enabled && IsKeyPressed(KEY_ENTER))) &&
        state.connectedToLobby) {
      SendText(lobby_connection, "Start!");
      start_sent = true;
    }

    const float axis_x = bench_options.enabled
                           ? socketwire_examples::benchmark::DeterministicAxis(
                               bench_options.seed, bench_frame, 0)
                           : ((IsKeyDown(KEY_RIGHT) ? 1.f : 0.f) +
                              (IsKeyDown(KEY_LEFT) ? -1.f : 0.f));
    const float axis_y = bench_options.enabled
                           ? socketwire_examples::benchmark::DeterministicAxis(
                               bench_options.seed, bench_frame, 1)
                           : ((IsKeyDown(KEY_DOWN) ? 1.f : 0.f) +
                              (IsKeyDown(KEY_UP) ? -1.f : 0.f));
    constexpr float accel = 30.f;
    velx += axis_x * dt * accel;
    vely += axis_y * dt * accel;
    posx += velx * dt;
    posy += vely * dt;
    velx *= 0.99f;
    vely *= 0.99f;
    const auto update_end = std::chrono::steady_clock::now();

    if (!bench_options.enabled) {
      BeginDrawing();
      ClearBackground(BLACK);

      DrawText(TextFormat("Current status: %s", state.gameServerStatus.c_str()),
               20, 20, 20, WHITE);
      DrawText(TextFormat("My position: (%d, %d)", static_cast<int>(posx),
                          static_cast<int>(posy)),
               20, 40, 20, WHITE);

      DrawCircleV(Vector2{posx, posy}, 10.f, WHITE);

      DrawText("List of players:", 20, 60, 20, WHITE);

      int y_offset = 80;
      for (const auto& player : state.players) {
        DrawText(TextFormat("Player %d: (%d, %d) - Ping: %d ms", player.id,
                            static_cast<int>(player.x),
                            static_cast<int>(player.y), player.ping),
                 20, y_offset, 18, WHITE);
        y_offset += 20;

        if (player.id != state.myPlayerId) {
          DrawCircleV(Vector2{player.x, player.y}, 10.f, RED);
          DrawText(TextFormat("%d", player.id), static_cast<int>(player.x - 5),
                   static_cast<int>(player.y - 5), 16, WHITE);
        }
      }

      EndDrawing();
    } else {
      int connected_count = state.connectedToLobby ? 1 : 0;
      if (state.connectedToGameServer) connected_count += 1;
      socketwire_examples::benchmark::NetworkStats stats =
        socketwire_examples::benchmark::StatsFromConnection(lobby_connection);
      if (game_connection != nullptr) {
        const auto game_stats =
          socketwire_examples::benchmark::StatsFromConnection(*game_connection);
        stats.rttMs = state.connectedToGameServer
                        ? (stats.rttMs + game_stats.rttMs) * 0.5
                        : stats.rttMs;
        stats.lostPackets += game_stats.lostPackets;
        stats.inflightPackets += game_stats.inflightPackets;
        stats.sendWindow += game_stats.sendWindow;
      }
      metrics.SetConnectedClients(connected_count);
      metrics.SetNetworkStats(stats);
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
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
      ++bench_frame;
    }
  }

  lobby_connection.Disconnect();
  if (game_connection != nullptr) game_connection->Disconnect();

  metrics.Finish();
  socketwire_examples::benchmark::SetActiveCollector(nullptr);
  if (!bench_options.enabled) CloseWindow();
  return 0;
}
