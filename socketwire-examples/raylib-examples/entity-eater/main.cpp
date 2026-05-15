#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "benchmark_utils.hpp"
#include "entity.h"
#include "protocol.h"
#include "raylib.h"
#include "reliable_connection.hpp"
#include "socketwire_example_utils.hpp"
#include "windows_defines.hpp"  // IWYU pragma: keep

static std::vector<Entity> entities;
static std::unordered_map<std::uint16_t, std::size_t> indexMap;
static std::uint16_t myEntity = INVALID_ENTITY;

static int gameTimeRemaining = 60;
static bool gameOver = false;
static std::uint16_t winnerEid = INVALID_ENTITY;
static int winnerScore = 0;

static void on_new_entity_packet(const void* data, std::size_t size) {
  Entity newEntity;
  deserialize_new_entity(data, size, newEntity);
  if (indexMap.contains(newEntity.eid)) return;

  indexMap[newEntity.eid] = entities.size();
  entities.push_back(newEntity);
}

static void on_set_controlled_entity(const void* data, std::size_t size) {
  deserialize_set_controlled_entity(data, size, myEntity);
}

template <typename Callable>
static void get_entity(std::uint16_t eid, Callable callable) {
  const auto it = indexMap.find(eid);
  if (it != indexMap.end()) callable(entities[it->second]);
}

static void on_snapshot(const void* data, std::size_t size) {
  std::uint16_t eid = INVALID_ENTITY;
  float x = 0.f;
  float y = 0.f;
  float entitySize = 0.f;
  deserialize_snapshot(data, size, eid, x, y, entitySize);
  get_entity(eid, [&](Entity& e) {
    e.x = x;
    e.y = y;
    e.size = entitySize;
  });
}

static void on_entity_devoured(const void* data, std::size_t size) {
  std::uint16_t devouredEid = INVALID_ENTITY;
  std::uint16_t devourerEid = INVALID_ENTITY;
  float newSize = 0.f;
  float newX = 0.f;
  float newY = 0.f;
  deserialize_entity_devoured(data, size, devouredEid, devourerEid, newSize,
                              newX, newY);

  get_entity(devourerEid, [&](Entity& e) { e.size = newSize; });
  get_entity(devouredEid, [&](Entity& e) {
    e.x = newX;
    e.y = newY;
  });
}

static void on_score_update(const void* data, std::size_t size) {
  std::uint16_t eid = INVALID_ENTITY;
  int score = 0;
  deserialize_score_update(data, size, eid, score);
  get_entity(eid, [&](Entity& e) { e.score = score; });
}

static void on_game_time(const void* data, std::size_t size) {
  int secondsRemaining = 0;
  deserialize_game_time(data, size, secondsRemaining);
  gameTimeRemaining = secondsRemaining;
}

static void on_game_over(const void* data, std::size_t size) {
  std::uint16_t wEid = INVALID_ENTITY;
  int wScore = 0;
  deserialize_game_over(data, size, wEid, wScore);

  gameOver = true;
  winnerEid = wEid;
  winnerScore = wScore;
  std::printf("Game Over! Winner is entity %u with score %d\n", winnerEid,
              winnerScore);
}

static bool compare_entity_scores(const Entity& a, const Entity& b) {
  return a.score > b.score;
}

class ClientHandler final : public socketwire::IReliableConnectionHandler {
 public:
  void OnConnected() override { connected = true; }
  void OnDisconnected() override { connected = false; }

  void OnReliableReceived(std::uint8_t channel, const void* data,
                          std::size_t size) override {
    socketwire_examples::benchmark::recordPayloadRx(size);
    processPacket(channel, data, size);
  }

  void OnUnreliableReceived(std::uint8_t channel, const void* data,
                            std::size_t size) override {
    socketwire_examples::benchmark::recordPayloadRx(size);
    processPacket(channel, data, size);
  }

  bool connected = false;

 private:
  static void processPacket([[maybe_unused]] std::uint8_t channel,
                            const void* data, std::size_t size) {
    switch (get_packet_type(data, size)) {
      case E_SERVER_TO_CLIENT_NEW_ENTITY:
        on_new_entity_packet(data, size);
        break;
      case E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY:
        on_set_controlled_entity(data, size);
        break;
      case E_SERVER_TO_CLIENT_SNAPSHOT:
        on_snapshot(data, size);
        break;
      case E_SERVER_TO_CLIENT_ENTITY_DEVOURED:
        on_entity_devoured(data, size);
        break;
      case E_SERVER_TO_CLIENT_SCORE_UPDATE:
        on_score_update(data, size);
        break;
      case E_SERVER_TO_CLIENT_GAME_TIME:
        on_game_time(data, size);
        break;
      case E_SERVER_TO_CLIENT_GAME_OVER:
        on_game_over(data, size);
        break;
      case E_CLIENT_TO_SERVER_JOIN:
      case E_CLIENT_TO_SERVER_STATE:
        break;
    }
  }
};

int main(int argc, const char** argv) {
  auto benchOptions =
    socketwire_examples::benchmark::parseOptions(argc, argv, 10131);
  socketwire_examples::benchmark::MetricsCollector metrics(
    benchOptions, "entity-eater", "socketwire", "client");
  socketwire_examples::benchmark::setActiveCollector(&metrics);

  const std::uint16_t connectPort =
    benchOptions.enabled
      ? benchOptions.port
      : socketwire_examples::portFromArgsOrEnv(
          argc, argv, 1, "SOCKETWIRE_ENTITY_EATER_PORT", 10131);

  auto socket = socketwire_examples::createUdpSocket(0);
  if (socket == nullptr) return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire::ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.SetHandler(&handler);
  connection.Connect(socketwire_examples::resolveAddress(benchOptions.host),
                     connectPort);

  int width = 800;
  int height = 600;
  if (!benchOptions.enabled)
    InitWindow(width, height, "Entity Eater - SocketWire");

  if (!benchOptions.enabled) {
    const int scrWidth = GetMonitorWidth(0);
    const int scrHeight = GetMonitorHeight(0);
    if (scrWidth < width || scrHeight < height) {
      width = std::min(scrWidth, width);
      height = std::min(scrHeight - 150, height);
      SetWindowSize(width, height);
    }
  }

  Camera2D camera = {{0.f, 0.f}, {0.f, 0.f}, 0.f, 1.f};
  camera.offset = Vector2{width * 0.5f, height * 0.5f};

  if (!benchOptions.enabled) SetTargetFPS(60);

  bool sentJoin = false;
  std::uint64_t benchFrame = 0;
  while (benchOptions.enabled ? !metrics.done() : !WindowShouldClose()) {
    const auto frameStart = std::chrono::steady_clock::now();
    const float dt = benchOptions.enabled ? (1.f / 60.f) : GetFrameTime();
    const auto updateStart = std::chrono::steady_clock::now();
    connection.Tick();

    if (handler.connected && !sentJoin) {
      send_join(&connection);
      sentJoin = true;
    }

    if (myEntity != INVALID_ENTITY) {
      const float axisX = benchOptions.enabled
                            ? socketwire_examples::benchmark::deterministicAxis(
                                benchOptions.seed, benchFrame, 0)
                            : ((IsKeyDown(KEY_RIGHT) ? 1.f : 0.f) +
                               (IsKeyDown(KEY_LEFT) ? -1.f : 0.f));
      const float axisY = benchOptions.enabled
                            ? socketwire_examples::benchmark::deterministicAxis(
                                benchOptions.seed, benchFrame, 1)
                            : ((IsKeyDown(KEY_DOWN) ? 1.f : 0.f) +
                               (IsKeyDown(KEY_UP) ? -1.f : 0.f));
      get_entity(myEntity, [&](Entity& e) {
        e.x += axisX * dt * 100.f;
        e.y += axisY * dt * 100.f;

        send_entity_state(&connection, myEntity, e.x, e.y);
        camera.target.x = e.x;
        camera.target.y = e.y;
      });
    }
    const auto updateEnd = std::chrono::steady_clock::now();

    if (!benchOptions.enabled) {
      BeginDrawing();
      ClearBackground(Color{40, 40, 40, 255});
      BeginMode2D(camera);
      for (const Entity& e : entities) {
        DrawCircle(static_cast<int>(e.x), static_cast<int>(e.y), e.size,
                   GetColor(e.color));

        char idText[10]{};
        std::snprintf(idText, sizeof(idText), "%u", e.eid);
        DrawText(idText, static_cast<int>(e.x - 10.f),
                 static_cast<int>(e.y - 10.f), 10, WHITE);
      }
      EndMode2D();

      if (myEntity != INVALID_ENTITY) {
        get_entity(myEntity, [&](Entity& e) {
          char scoreText[50]{};
          std::snprintf(scoreText, sizeof(scoreText), "Your Score: %d",
                        e.score);
          DrawText(scoreText, 10, 10, 20, WHITE);

          char sizeText[50]{};
          std::snprintf(sizeText, sizeof(sizeText), "Size: %.1f", e.size);
          DrawText(sizeText, 10, 40, 20, WHITE);
        });
      }

      char timeText[50]{};
      std::snprintf(timeText, sizeof(timeText), "Time: %d", gameTimeRemaining);
      DrawText(timeText, width / 2 - 50, 10, 30, YELLOW);

      DrawRectangle(width - 200, 10, 190, 210, Color{0, 0, 0, 150});
      DrawText("LEADERBOARD", width - 190, 15, 20, YELLOW);

      std::vector<Entity> sortedEntities = entities;
      std::sort(sortedEntities.begin(), sortedEntities.end(),
                compare_entity_scores);

      const int maxToShow =
        std::min(8, static_cast<int>(sortedEntities.size()));
      for (int i = 0; i < maxToShow; ++i) {
        const Entity& e = sortedEntities[static_cast<std::size_t>(i)];
        const char* playerType = e.serverControlled ? "AI" : "Player";
        const Color textColor = e.eid == myEntity ? GREEN : WHITE;

        char playerText[100]{};
        std::snprintf(playerText, sizeof(playerText), "%d. %s %u - Score: %d",
                      i + 1, playerType, e.eid, e.score);
        DrawText(playerText, width - 190, 45 + (i * 20), 15, textColor);
      }

      if (gameOver) {
        DrawRectangle(0, 0, width, height, Color{0, 0, 0, 200});
        DrawText("GAME OVER", width / 2 - 150, height / 2 - 100, 50, RED);

        std::string winnerType = "Unknown";
        Color winnerColor = WHITE;
        get_entity(winnerEid, [&](Entity& e) {
          winnerType = e.serverControlled ? "AI" : "Player";
          winnerColor = GetColor(e.color);
        });

        char winnerText[100]{};
        std::snprintf(winnerText, sizeof(winnerText), "Winner: %s %u",
                      winnerType.c_str(), winnerEid);
        DrawText(winnerText, width / 2 - 120, height / 2, 30, winnerColor);

        char scoreText[50]{};
        std::snprintf(scoreText, sizeof(scoreText), "Final Score: %d",
                      winnerScore);
        DrawText(scoreText, width / 2 - 100, height / 2 + 50, 30, YELLOW);
      }
      EndDrawing();
    } else {
      metrics.setConnectedClients(handler.connected ? 1 : 0);
      metrics.setNetworkStats(
        socketwire_examples::benchmark::statsFromConnection(connection));
      metrics.recordUpdateMs(
        static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(updateEnd -
                                                                updateStart)
            .count()) /
        1000.0);
      metrics.recordFrameMs(
        static_cast<double>(
          std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - frameStart)
            .count()) /
        1000.0);
      metrics.maybeWriteSample();
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
      ++benchFrame;
    }
  }

  connection.Disconnect();
  metrics.finish();
  socketwire_examples::benchmark::setActiveCollector(nullptr);
  if (!benchOptions.enabled) CloseWindow();
  return 0;
}
