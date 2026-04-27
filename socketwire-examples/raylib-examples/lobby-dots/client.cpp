#include "raylib.h"

#include "reliable_connection.hpp"
#include "socketwire_example_utils.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
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
  if (reliable)
    connection.sendReliable(0, text.c_str(), text.size() + 1);
  else
    connection.sendUnsequenced(0, text.c_str(), text.size() + 1);
}

static void send_fragmented_packet(socketwire::ReliableConnection& connection)
{
  const char* baseMsg = "Stay awhile and listen. ";
  const std::size_t msgLen = std::char_traits<char>::length(baseMsg);

  constexpr std::size_t sendSize = 2500;
  std::string hugeMessage(sendSize, '\0');
  for (std::size_t i = 0; i < sendSize - 1; ++i)
    hugeMessage[i] = baseMsg[i % msgLen];

  connection.sendReliable(0, hugeMessage.c_str(), hugeMessage.size());
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

  void onConnected() override
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

  void onDisconnected() override
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

  void onReliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
  {
    handlePacket(channel, data, size);
  }

  void onUnreliableReceived(std::uint8_t channel, const void* data, std::size_t size) override
  {
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

int main()
{
  int width = 800;
  int height = 600;
  InitWindow(width, height, "Lobby Dots");

  const int scrWidth = GetMonitorWidth(0);
  const int scrHeight = GetMonitorHeight(0);
  if (scrWidth < width || scrHeight < height)
  {
    width = std::min(scrWidth, width);
    height = std::min(scrHeight - 150, height);
    SetWindowSize(width, height);
  }

  SetTargetFPS(60);

  auto lobbySocket = socketwire_examples::createUdpSocket(0);
  if (lobbySocket == nullptr)
    return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire::ReliableConnection lobbyConnection(lobbySocket.get(), cfg);

  ClientState state;
  ClientHandler lobbyHandler(state, ConnectionTarget::Lobby);
  lobbyConnection.setHandler(&lobbyHandler);
  lobbyConnection.connect(socketwire::SocketConstants::loopback(), 10887);

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

  while (!WindowShouldClose())
  {
    const float dt = GetFrameTime();

    lobbyConnection.tick();
    if (gameConnection != nullptr)
      gameConnection->tick();

    if (!state.pendingGameHost.empty() && gameConnection == nullptr)
    {
      gameSocket = socketwire_examples::createUdpSocket(0);
      if (gameSocket != nullptr)
      {
        gameConnection = std::make_unique<socketwire::ReliableConnection>(gameSocket.get(), cfg);
        gameHandler = std::make_unique<ClientHandler>(state, ConnectionTarget::Game);
        gameConnection->setHandler(gameHandler.get());
        gameConnection->connect(
          socketwire_examples::resolveAddress(state.pendingGameHost),
          state.pendingGamePort);
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

    if (IsKeyPressed(KEY_ESCAPE))
      break;

    if (IsKeyPressed(KEY_ENTER) && state.connectedToLobby)
      send_text(lobbyConnection, "Start!");

    const bool left = IsKeyDown(KEY_LEFT);
    const bool right = IsKeyDown(KEY_RIGHT);
    const bool up = IsKeyDown(KEY_UP);
    const bool down = IsKeyDown(KEY_DOWN);
    constexpr float accel = 30.f;
    velx += ((left ? -1.f : 0.f) + (right ? 1.f : 0.f)) * dt * accel;
    vely += ((up ? -1.f : 0.f) + (down ? 1.f : 0.f)) * dt * accel;
    posx += velx * dt;
    posy += vely * dt;
    velx *= 0.99f;
    vely *= 0.99f;

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

  lobbyConnection.disconnect();
  if (gameConnection != nullptr)
    gameConnection->disconnect();

  CloseWindow();
  return 0;
}
