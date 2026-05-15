#include "raylib.h"

#include "benchmark_utils.hpp"
#include "reliable_connection.hpp"
#include "socketwire_example_utils.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
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
};

struct ClientState
{
  bool connectedToLobby = false;
  bool connectedToGameServer = false;
  std::string gameServerStatus = "Connecting to lobby...";
  std::vector<Player> players;
  int myPlayerId = -1;
  std::string pendingGameHost;
  std::uint16_t pendingGamePort = 0;
};

static void send_text(socketwire::ReliableConnection& connection,
                      const std::string& text,
                      bool reliable = true)
{
  const std::size_t bytes = text.size() + 1;
  const bool sent = reliable
    ? connection.SendReliable(0, text.c_str(), bytes)
    : connection.SendUnsequenced(0, text.c_str(), bytes);
  if (sent)
    socketwire_examples::benchmark::recordPayloadTx(bytes);
}

static void send_fragmented_packet(socketwire::ReliableConnection& connection)
{
  const char* baseMsg = "Stay awhile and listen. ";
  const std::size_t msgLen = std::char_traits<char>::length(baseMsg);

  constexpr std::size_t SEND_SIZE = 2500;
  std::string hugeMessage(SEND_SIZE, '\0');
  for (std::size_t i = 0; i < SEND_SIZE - 1; ++i)
    hugeMessage[i] = baseMsg[i % msgLen];

  if (connection.SendReliable(0, hugeMessage.c_str(), hugeMessage.size()))
    socketwire_examples::benchmark::recordPayloadTx(hugeMessage.size());
}

static void send_micro_packet(socketwire::ReliableConnection& connection)
{
  send_text(connection, "dv/dt", false);
}

static void send_position(socketwire::ReliableConnection& connection, float x, float y)
{
  char posMsg[64]{};
  std::snprintf(posMsg, sizeof(posMsg), "POS %.2f %.2f", x, y);
  send_text(connection, posMsg, false);
}

enum class ConnectionTarget
{
  Lobby,
  Game
};

class ClientHandler final : public socketwire::IReliableConnectionHandler
{
public:
  ClientHandler(ClientState& state, ConnectionTarget target)
    : state_(state)
    , target_(target)
  {
  }

  void OnConnected() override
  {
    if (target_ == ConnectionTarget::Lobby)
    {
      state_.connectedToLobby = true;
      state_.gameServerStatus = "Connected to lobby";
    }
    else
    {
      state_.connectedToGameServer = true;
      state_.gameServerStatus = "Connected to game server";
    }
  }

  void OnDisconnected() override
  {
    if (target_ == ConnectionTarget::Lobby)
    {
      state_.connectedToLobby = false;
      state_.gameServerStatus = "Disconnected from lobby";
    }
    else
    {
      state_.connectedToGameServer = false;
      state_.gameServerStatus = "Disconnected from game server";
    }
  }

  void OnReliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
  {
    socketwire_examples::benchmark::recordPayloadRx(size);
    handlePacket(channel, data, size);
  }

  void OnUnreliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
  {
    socketwire_examples::benchmark::recordPayloadRx(size);
    handlePacket(channel, data, size);
  }

private:
  ClientState& state_;
  ConnectionTarget target_;

  void handlePacket([[maybe_unused]] std::uint8_t channel, const void* data, std::size_t size)
  {
    const std::string text = socketwire_examples::readStringPayload(data, size);
    std::printf("Packet received '%s'\n", text.c_str());

    if (target_ == ConnectionTarget::Lobby)
    {
      if (!state_.connectedToGameServer && text.starts_with("GAMESERVER"))
      {
        char serverIP[256]{};
        int serverPort = 0;
        if (std::sscanf(text.c_str(), "GAMESERVER %255s %d", serverIP, &serverPort) == 2)
        {
          state_.pendingGameHost = serverIP;
          state_.pendingGamePort = static_cast<std::uint16_t>(serverPort);
          state_.gameServerStatus = "Connecting to game server...";
        }
      }
      return;
    }

    if (text.starts_with("WELCOME"))
    {
      int id = -1;
      char name[256]{};
      if (std::sscanf(text.c_str(), "WELCOME %d %255s", &id, name) >= 1)
      {
        state_.myPlayerId = id;
        state_.gameServerStatus = "Playing as player " + std::to_string(state_.myPlayerId);
      }
    }
    else if (text.starts_with("PLAYERS"))
    {
      state_.players.clear();
      std::istringstream ss(text.substr(8));
      Player player;
      std::string token;

      while (std::getline(ss, token, ';'))
      {
        std::istringstream playerStream(token);
        if (playerStream >> player.id >> player.x >> player.y >> player.ping)
          state_.players.push_back(player);
      }
    }
    else if (text.starts_with("POS"))
    {
      int playerId = -1;
      float x = 0.f;
      float y = 0.f;
      if (std::sscanf(text.c_str(), "POS %d %f %f", &playerId, &x, &y) == 3)
      {
        for (auto& player : state_.players)
        {
          if (player.id == playerId)
          {
            player.x = x;
            player.y = y;
            break;
          }
        }
      }
    }
    else if (text.starts_with("NEWPLAYER"))
    {
      int playerId = -1;
      char playerName[256]{};
      if (std::sscanf(text.c_str(), "NEWPLAYER %d %255s", &playerId, playerName) >= 1)
      {
        const auto exists = std::ranges::any_of(
          state_.players, [playerId](const Player& player) { return player.id == playerId; });

        if (!exists)
          state_.players.push_back(Player{playerId, 0.f, 0.f, 0});
      }
    }
    else if (text.starts_with("PLAYERLEFT"))
    {
      int playerId = -1;
      if (std::sscanf(text.c_str(), "PLAYERLEFT %d", &playerId) == 1)
        std::erase_if(state_.players, [playerId](const Player& player) { return player.id == playerId; });
    }
    else if (text.starts_with("PINGS"))
    {
      std::printf("Processing ping data: %s\n", text.c_str());
      std::istringstream ss(text.substr(6));
      std::string token;
      while (std::getline(ss, token, ';'))
      {
        if (token.empty())
          continue;

        int playerId = -1;
        int pingValue = 0;
        std::istringstream pingStream(token);
        if (pingStream >> playerId >> pingValue)
        {
          bool updated = false;
          for (auto& player : state_.players)
          {
            if (player.id == playerId)
            {
              player.ping = pingValue;
              updated = true;
              break;
            }
          }

          if (!updated && playerId != state_.myPlayerId)
            state_.players.push_back(Player{playerId, 0.f, 0.f, pingValue});
        }
      }
    }
  }
};

int main(int argc, const char** argv)
{
  auto benchOptions = socketwire_examples::benchmark::parseOptions(argc, argv, 0, 10887, 10888);
  socketwire_examples::benchmark::MetricsCollector metrics(
    benchOptions, "lobby-dots", "socketwire", "client");
  socketwire_examples::benchmark::setActiveCollector(&metrics);

  const std::uint16_t connectLobbyPort = benchOptions.enabled
    ? benchOptions.lobbyPort
    : socketwire_examples::portFromArgsOrEnv(argc, argv, 1, "SOCKETWIRE_LOBBY_DOTS_LOBBY_PORT", 10887);

  int width = 800;
  int height = 600;
  if (!benchOptions.enabled)
    InitWindow(width, height, "Lobby Dots");

  if (!benchOptions.enabled)
  {
    const int scrWidth = GetMonitorWidth(0);
    const int scrHeight = GetMonitorHeight(0);
    if (scrWidth < width || scrHeight < height)
    {
      width = std::min(scrWidth, width);
      height = std::min(scrHeight - 150, height);
      SetWindowSize(width, height);
    }
  }

  if (!benchOptions.enabled)
    SetTargetFPS(60);

  auto lobbySocket = socketwire_examples::createUdpSocket(0);
  if (lobbySocket == nullptr)
    return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire::ReliableConnection lobbyConnection(lobbySocket.get(), cfg);

  ClientState state;
  ClientHandler lobbyHandler(state, ConnectionTarget::Lobby);
  lobbyConnection.SetHandler(&lobbyHandler);
  lobbyConnection.Connect(socketwire_examples::resolveAddress(benchOptions.host), connectLobbyPort);

  std::unique_ptr<socketwire::ISocket> gameSocket;
  std::unique_ptr<socketwire::ReliableConnection> gameConnection;
  std::unique_ptr<ClientHandler> gameHandler;

  auto lastFragmentedSendTime = std::chrono::steady_clock::now();
  auto lastMicroSendTime = lastFragmentedSendTime;
  auto lastPositionSendTime = lastFragmentedSendTime;

  float posx = static_cast<float>(GetRandomValue(100, 500));
  float posy = static_cast<float>(GetRandomValue(100, 500));
  float velx = 0.f;
  float vely = 0.f;
  bool startSent = false;
  std::uint64_t benchFrame = 0;

  while (benchOptions.enabled ? !metrics.done() : !WindowShouldClose())
  {
    const auto frameStart = std::chrono::steady_clock::now();
    const float dt = benchOptions.enabled ? (1.f / 60.f) : GetFrameTime();

    const auto updateStart = std::chrono::steady_clock::now();
    lobbyConnection.Tick();
    if (gameConnection != nullptr)
      gameConnection->Tick();

    if (!state.pendingGameHost.empty() && gameConnection == nullptr)
    {
      gameSocket = socketwire_examples::createUdpSocket(0);
      if (gameSocket != nullptr)
      {
        gameConnection = std::make_unique<socketwire::ReliableConnection>(gameSocket.get(), cfg);
        gameHandler = std::make_unique<ClientHandler>(state, ConnectionTarget::Game);
        gameConnection->SetHandler(gameHandler.get());
        gameConnection->Connect(socketwire_examples::resolveAddress(state.pendingGameHost), state.pendingGamePort);
      }
      else
      {
        state.gameServerStatus = "Cannot connect to game server";
      }
      state.pendingGameHost.clear();
    }

    const auto now = std::chrono::steady_clock::now();

    if (state.connectedToGameServer && gameConnection != nullptr &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPositionSendTime).count() > 50)
    {
      lastPositionSendTime = now;
      send_position(*gameConnection, posx, posy);
    }

    if (state.connectedToLobby)
    {
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFragmentedSendTime).count() > 1000)
      {
        lastFragmentedSendTime = now;
        // send_fragmented_packet(lobbyConnection);
        (void)send_fragmented_packet;
      }
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMicroSendTime).count() > 100)
      {
        lastMicroSendTime = now;
        // send_micro_packet(lobbyConnection);
        (void)send_micro_packet;
      }
    }

    if (!benchOptions.enabled && IsKeyPressed(KEY_ESCAPE))
      break;

    if (((benchOptions.enabled && !startSent) || (!benchOptions.enabled && IsKeyPressed(KEY_ENTER))) &&
        state.connectedToLobby)
    {
      send_text(lobbyConnection, "Start!");
      startSent = true;
    }

    const float axisX = benchOptions.enabled
      ? socketwire_examples::benchmark::deterministicAxis(benchOptions.seed, benchFrame, 0)
      : ((IsKeyDown(KEY_RIGHT) ? 1.f : 0.f) + (IsKeyDown(KEY_LEFT) ? -1.f : 0.f));
    const float axisY = benchOptions.enabled
      ? socketwire_examples::benchmark::deterministicAxis(benchOptions.seed, benchFrame, 1)
      : ((IsKeyDown(KEY_DOWN) ? 1.f : 0.f) + (IsKeyDown(KEY_UP) ? -1.f : 0.f));
    constexpr float ACCEL = 30.f;
    velx += axisX * dt * ACCEL;
    vely += axisY * dt * ACCEL;
    posx += velx * dt;
    posy += vely * dt;
    velx *= 0.99f;
    vely *= 0.99f;
    const auto updateEnd = std::chrono::steady_clock::now();

    if (!benchOptions.enabled)
    {
      BeginDrawing();
      ClearBackground(BLACK);

      DrawText(TextFormat("Current status: %s", state.gameServerStatus.c_str()), 20, 20, 20, WHITE);
      DrawText(TextFormat("My position: (%d, %d)", static_cast<int>(posx), static_cast<int>(posy)), 20, 40, 20, WHITE);

      DrawCircleV(Vector2{posx, posy}, 10.f, WHITE);

      DrawText("List of players:", 20, 60, 20, WHITE);

      int yOffset = 80;
      for (const auto& player : state.players)
      {
        DrawText(TextFormat("Player %d: (%d, %d) - Ping: %d ms",
                            player.id,
                            static_cast<int>(player.x),
                            static_cast<int>(player.y),
                            player.ping),
                 20,
                 yOffset,
                 18,
                 WHITE);
        yOffset += 20;

        if (player.id != state.myPlayerId)
        {
          DrawCircleV(Vector2{player.x, player.y}, 10.f, RED);
          DrawText(TextFormat("%d", player.id), static_cast<int>(player.x - 5), static_cast<int>(player.y - 5), 16, WHITE);
        }
      }

    EndDrawing();
    }
    else
    {
      int connectedCount = state.connectedToLobby ? 1 : 0;
      if (state.connectedToGameServer)
        connectedCount += 1;
      socketwire_examples::benchmark::NetworkStats stats =
        socketwire_examples::benchmark::statsFromConnection(lobbyConnection);
      if (gameConnection != nullptr)
      {
        const auto gameStats = socketwire_examples::benchmark::statsFromConnection(*gameConnection);
        stats.rttMs = state.connectedToGameServer ? (stats.rttMs + gameStats.rttMs) * 0.5 : stats.rttMs;
        stats.lostPackets += gameStats.lostPackets;
        stats.inflightPackets += gameStats.inflightPackets;
        stats.sendWindow += gameStats.sendWindow;
      }
      metrics.setConnectedClients(connectedCount);
      metrics.setNetworkStats(stats);
      metrics.recordUpdateMs(static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(updateEnd - updateStart).count()) / 1000.0);
      metrics.recordFrameMs(static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - frameStart).count()) / 1000.0);
      metrics.maybeWriteSample();
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
      ++benchFrame;
    }
  }

  lobbyConnection.Disconnect();
  if (gameConnection != nullptr)
    gameConnection->Disconnect();

  metrics.finish();
  socketwire_examples::benchmark::setActiveCollector(nullptr);
  if (!benchOptions.enabled)
    CloseWindow();
  return 0;
}
