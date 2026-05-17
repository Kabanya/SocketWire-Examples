#include <algorithm>
#include <chrono>
#include <cstdio>
#include <print>
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
static std::unordered_map<std::uint16_t, std::size_t> index_map;
static std::uint16_t my_entity = kInvalidEntity;

static int game_time_remaining = 60;
static bool game_over = false;
static std::uint16_t winner_eid = kInvalidEntity;
static int winner_score = 0;

static void OnNewEntityPacket(const void* data, std::size_t size) {
  Entity new_entity;
  DeserializeNewEntity(data, size, new_entity);
  if (index_map.contains(new_entity.eid)) return;

  index_map[new_entity.eid] = entities.size();
  entities.push_back(new_entity);
}

static void OnSetControlledEntity(const void* data, std::size_t size) {
  DeserializeSetControlledEntity(data, size, my_entity);
}

template <typename Callable>
static void GetEntity(std::uint16_t eid, Callable callable) {
  const auto it = index_map.find(eid);
  if (it != index_map.end()) callable(entities[it->second]);
}

static void OnSnapshot(const void* data, std::size_t size) {
  std::uint16_t eid = kInvalidEntity;
  float x = 0.f;
  float y = 0.f;
  float entity_size = 0.f;
  DeserializeSnapshot(data, size, eid, x, y, entity_size);
  GetEntity(eid, [&](Entity& e) {
    if (eid != my_entity) {
      e.x = x;
      e.y = y;
    }
    e.size = entity_size;
  });
}

static void OnEntityDevoured(const void* data, std::size_t size) {
  std::uint16_t devoured_eid = kInvalidEntity;
  std::uint16_t devourer_eid = kInvalidEntity;
  float devourer_new_size = 0.f;
  float devoured_new_size = 0.f;
  float new_x = 0.f;
  float new_y = 0.f;
  DeserializeEntityDevoured(data, size, devoured_eid, devourer_eid,
                            devourer_new_size, devoured_new_size, new_x, new_y);

  GetEntity(devourer_eid, [&](Entity& e) { e.size = devourer_new_size; });
  GetEntity(devoured_eid, [&](Entity& e) {
    e.x = new_x;
    e.y = new_y;
    e.size = devoured_new_size;
  });
}

static void OnScoreUpdate(const void* data, std::size_t size) {
  std::uint16_t eid = kInvalidEntity;
  int score = 0;
  DeserializeScoreUpdate(data, size, eid, score);
  GetEntity(eid, [&](Entity& e) { e.score = score; });
}

static void OnGameTime(const void* data, std::size_t size) {
  int seconds_remaining = 0;
  DeserializeGameTime(data, size, seconds_remaining);
  game_time_remaining = seconds_remaining;
}

static void OnGameOver(const void* data, std::size_t size) {
  std::uint16_t w_eid = kInvalidEntity;
  int w_score = 0;
  DeserializeGameOver(data, size, w_eid, w_score);

  game_over = true;
  winner_eid = w_eid;
  winner_score = w_score;
  std::println("Game Over! Winner is entity {} with score {}", winner_eid,
               winner_score);
}

static bool CompareEntityScores(const Entity& a, const Entity& b) {
  return a.score > b.score;
}

class ClientHandler final : public socketwire::IReliableConnectionHandler {
 public:
  void OnConnected() override { connected = true; }
  void OnDisconnected() override { connected = false; }

  void OnReliableReceived(std::uint8_t channel, const void* data,
                          std::size_t size) override {
    socketwire_examples::benchmark::RecordPayloadRx(size);
    ProcessPacket(channel, data, size);
  }

  void OnUnreliableReceived(std::uint8_t channel, const void* data,
                            std::size_t size) override {
    socketwire_examples::benchmark::RecordPayloadRx(size);
    ProcessPacket(channel, data, size);
  }

  bool connected = false;

 private:
  static void ProcessPacket([[maybe_unused]] std::uint8_t channel,
                            const void* data, std::size_t size) {
    switch (GetPacketType(data, size)) {
      case MessageType::kEServerToClientNewEntity:
        OnNewEntityPacket(data, size);
        break;
      case MessageType::kEServerToClientSetControlledEntity:
        OnSetControlledEntity(data, size);
        break;
      case MessageType::kEServerToClientSnapshot:
        OnSnapshot(data, size);
        break;
      case MessageType::kEServerToClientEntityDevoured:
        OnEntityDevoured(data, size);
        break;
      case MessageType::kEServerToClientScoreUpdate:
        OnScoreUpdate(data, size);
        break;
      case MessageType::kEServerToClientGameTime:
        OnGameTime(data, size);
        break;
      case MessageType::kEServerToClientGameOver:
        OnGameOver(data, size);
        break;
      case MessageType::kEClientToServerJoin:
      case MessageType::kEClientToServerState:
        break;
    }
  }
};

int main(int argc, const char** argv) {
  auto bench_options =
    socketwire_examples::benchmark::ParseOptions(argc, argv, 10131);
  socketwire_examples::benchmark::MetricsCollector metrics(
    bench_options, "entity-eater", "socketwire", "client");
  socketwire_examples::benchmark::SetActiveCollector(&metrics);

  const std::uint16_t connect_port =
    bench_options.enabled
      ? bench_options.port
      : socketwire_examples::PortFromArgsOrEnv(
          argc, argv, 1, "SOCKETWIRE_ENTITY_EATER_PORT", 10131);

  auto socket = socketwire_examples::CreateUdpSocket(0);
  if (socket == nullptr) return 1;

  socketwire::ReliableConnectionConfig cfg;
  cfg.numChannels = 2;
  socketwire::ReliableConnection connection(socket.get(), cfg);
  ClientHandler handler;
  connection.SetHandler(&handler);
  connection.Connect(socketwire_examples::ResolveAddress(bench_options.host),
                     connect_port);

  int width = 800;
  int height = 600;
  if (!bench_options.enabled) {
    InitWindow(width, height, "Entity Eater - SocketWire");
  }

  if (!bench_options.enabled) {
    const int scr_width = GetMonitorWidth(0);
    const int scr_height = GetMonitorHeight(0);
    if (scr_width < width || scr_height < height) {
      width = std::min(scr_width, width);
      height = std::min(scr_height - 150, height);
      SetWindowSize(width, height);
    }
  }

  Camera2D camera = {{0.f, 0.f}, {0.f, 0.f}, 0.f, 1.f};
  camera.offset = Vector2{static_cast<float>(width) * 0.5f,
                          static_cast<float>(height) * 0.5f};

  if (!bench_options.enabled) SetTargetFPS(60);

  bool sent_join = false;
  std::uint64_t bench_frame = 0;
  while (bench_options.enabled ? !metrics.Done() : !WindowShouldClose()) {
    const auto frame_start = std::chrono::steady_clock::now();
    const float dt = bench_options.enabled ? (1.f / 60.f) : GetFrameTime();
    const auto update_start = std::chrono::steady_clock::now();
    connection.Tick();

    if (handler.connected && !sent_join) {
      SendJoin(&connection);
      sent_join = true;
    }

    if (my_entity != kInvalidEntity) {
      const float axis_x =
        bench_options.enabled
          ? socketwire_examples::benchmark::DeterministicAxis(
              bench_options.seed, bench_frame, 0)
          : ((IsKeyDown(KEY_RIGHT) ? 1.f : 0.f) +
             (IsKeyDown(KEY_LEFT) ? -1.f : 0.f));
      const float axis_y =
        bench_options.enabled
          ? socketwire_examples::benchmark::DeterministicAxis(
              bench_options.seed, bench_frame, 1)
          : ((IsKeyDown(KEY_DOWN) ? 1.f : 0.f) +
             (IsKeyDown(KEY_UP) ? -1.f : 0.f));
      GetEntity(my_entity, [&](Entity& e) {
        e.x += axis_x * dt * 100.f;
        e.y += axis_y * dt * 100.f;

        SendEntityState(&connection, my_entity, e.x, e.y);
        camera.target.x = e.x;
        camera.target.y = e.y;
      });
    }
    const auto update_end = std::chrono::steady_clock::now();

    if (!bench_options.enabled) {
      BeginDrawing();
      ClearBackground(Color{40, 40, 40, 255});
      BeginMode2D(camera);
      for (const Entity& e : entities) {
        DrawCircle(static_cast<int>(e.x), static_cast<int>(e.y), e.size,
                   GetColor(e.color));

        char id_text[10]{};
        std::snprintf(id_text, sizeof(id_text), "%u", e.eid);
        DrawText(id_text, static_cast<int>(e.x - 10.f),
                 static_cast<int>(e.y - 10.f), 10, WHITE);
      }
      EndMode2D();

      if (my_entity != kInvalidEntity) {
        GetEntity(my_entity, [&](Entity& e) {
          char score_text[50]{};
          std::snprintf(score_text, sizeof(score_text), "Your Score: %d",
                        e.score);
          DrawText(score_text, 10, 10, 20, WHITE);

          char size_text[50]{};
          std::snprintf(size_text, sizeof(size_text), "Size: %.1f", e.size);
          DrawText(size_text, 10, 40, 20, WHITE);
        });
      }

      char time_text[50]{};
      std::snprintf(time_text, sizeof(time_text), "Time: %d",
                    game_time_remaining);
      DrawText(time_text, width / 2 - 50, 10, 30, YELLOW);

      DrawRectangle(width - 200, 10, 190, 210, Color{0, 0, 0, 150});
      DrawText("LEADERBOARD", width - 190, 15, 20, YELLOW);

      std::vector<Entity> sorted_entities = entities;
      std::sort(sorted_entities.begin(), sorted_entities.end(),
                CompareEntityScores);

      const int max_to_show =
        std::min(8, static_cast<int>(sorted_entities.size()));
      for (int i = 0; i < max_to_show; ++i) {
        const Entity& e = sorted_entities[static_cast<std::size_t>(i)];
        const char* player_type = e.serverControlled ? "AI" : "Player";
        const Color text_color = e.eid == my_entity ? GREEN : WHITE;

        char player_text[100]{};
        std::snprintf(player_text, sizeof(player_text), "%d. %s %u - Score: %d",
                      i + 1, player_type, e.eid, e.score);
        DrawText(player_text, width - 190, 45 + (i * 20), 15, text_color);
      }

      if (game_over) {
        DrawRectangle(0, 0, width, height, Color{0, 0, 0, 200});
        DrawText("GAME OVER", width / 2 - 150, height / 2 - 100, 50, RED);

        std::string winner_type = "Unknown";
        auto winner_color = WHITE;
        GetEntity(winner_eid, [&](Entity& e) {
          winner_type = e.serverControlled ? "AI" : "Player";
          winner_color = GetColor(e.color);
        });

        char winner_text[100]{};
        std::snprintf(winner_text, sizeof(winner_text), "Winner: %s %u",
                      winner_type.c_str(), winner_eid);
        DrawText(winner_text, width / 2 - 120, height / 2, 30, winner_color);

        char score_text[50]{};
        std::snprintf(score_text, sizeof(score_text), "Final Score: %d",
                      winner_score);
        DrawText(score_text, width / 2 - 100, height / 2 + 50, 30, YELLOW);
      }
      EndDrawing();
    } else {
      metrics.SetConnectedClients(handler.connected ? 1 : 0);
      metrics.SetNetworkStats(
        socketwire_examples::benchmark::StatsFromConnection(connection));
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

  connection.Disconnect();
  metrics.Finish();
  socketwire_examples::benchmark::SetActiveCollector(nullptr);
  if (!bench_options.enabled) CloseWindow();
  return 0;
}
