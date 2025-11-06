#include <algorithm> // min/max
#include <cstdio>    // printf
#include <enet/enet.h>
#include <unordered_map>
#include <vector>
#include <string>

#include "raylib.h"
#include "entity.h"
#include "protocol.h"


static std::vector<Entity> entities;
static std::unordered_map<uint16_t, size_t> indexMap;
static uint16_t myEntity = INVALID_ENTITY;

static int gameTimeRemaining = 60;
static bool gameOver = false;
static uint16_t winnerEid = INVALID_ENTITY;
static int winnerScore = 0;

void on_new_entity_packet(ENetPacket *packet)
{
  Entity newEntity;
  deserialize_new_entity(packet, newEntity);
  auto itf = indexMap.find(newEntity.eid);
  if (itf != indexMap.end())
    return; // ничего не делаем если есть entity
  indexMap[newEntity.eid] = entities.size();
  entities.push_back(newEntity);
}

void on_set_controlled_entity(ENetPacket *packet)
{
  deserialize_set_controlled_entity(packet, myEntity);
}

template<typename Callable>
static void get_entity(uint16_t eid, Callable c)
{
  auto itf = indexMap.find(eid);
  if (itf != indexMap.end())
    c(entities[itf->second]);
}

void on_snapshot(ENetPacket *packet)
{
  uint16_t eid = INVALID_ENTITY;
  float x = 0.f; float y = 0.f; float size = 0.f;
  deserialize_snapshot(packet, eid, x, y, size);
  get_entity(eid, [&](Entity& e)
  {
    e.x = x;
    e.y = y;
    e.size = size;
  });
}

void on_entity_devoured(ENetPacket *packet)
{
  uint16_t devouredEid = INVALID_ENTITY;
  uint16_t devourerEid = INVALID_ENTITY;
  float newSize = 0.f;
  float newX = 0.f;
  float newY = 0.f;

  deserialize_entity_devoured(packet, devouredEid, devourerEid, newSize, newX, newY);

  get_entity(devourerEid, [&](Entity& e)
  {
    e.size = newSize;
  });

  get_entity(devouredEid, [&](Entity& e)
  {
    e.x = newX;
    e.y = newY;
  });
}

// Handle score update event
void on_score_update(ENetPacket *packet)
{
  uint16_t eid = INVALID_ENTITY;
  int score = 0;

  deserialize_score_update(packet, eid, score);

  get_entity(eid, [&](Entity& e)
  {
    e.score = score;
  });
}

void on_game_time(ENetPacket *packet)
{
  int secondsRemaining = 0;

  deserialize_game_time(packet, secondsRemaining);
  gameTimeRemaining = secondsRemaining;
}

void on_game_over(ENetPacket *packet)
{
  uint16_t wEid = INVALID_ENTITY;
  int wScore = 0;

  deserialize_game_over(packet, wEid, wScore);

  gameOver = true;
  winnerEid = wEid;
  winnerScore = wScore;

  printf("Game Over! Winner is entity %d with score %d\n", winnerEid, winnerScore);
}

bool compare_entity_scores(const Entity& a, const Entity& b)
{
  return a.score > b.score;
}

// int main(int argc, const char **argv)
int main()
{
  if (enet_initialize() != 0)
  {
    printf("Cannot init ENet");
    return 1;
  }

  ENetHost *client = enet_host_create(nullptr, 1, 2, 0, 0);
  if (client == nullptr)
  {
    printf("Cannot create ENet client\n");
    return 1;
  }

  ENetAddress address;
  enet_address_set_host(&address, "127.0.0.1");
  address.port = 10131;

  ENetPeer *serverPeer = enet_host_connect(client, &address, 2, 0);
  if (serverPeer == nullptr)
  {
    printf("Cannot connect to server");
    return 1;
  }

  int width = 800;
  int height = 600;
  InitWindow(width, height, "w4 networked MIPT");

  const int scrWidth = GetMonitorWidth(0);
  const int scrHeight = GetMonitorHeight(0);
  if (scrWidth < width || scrHeight < height)
  {
    width = std::min(scrWidth, width);
    height = std::min(scrHeight - 150, height);
    SetWindowSize(width, height);
  }

  Camera2D camera = { {0, 0}, {0, 0}, 0.f, 1.f };
  camera.target = Vector2{ 0.f, 0.f };
  camera.offset = Vector2{ width * 0.5f, height * 0.5f };
  camera.rotation = 0.f;
  camera.zoom = 1.f;

  SetTargetFPS(60);               // Set our game to run at 60 frames-per-second

  [[maybe_unused]]
  bool connected = false;
  while (!WindowShouldClose())
  {
    float dt = GetFrameTime();
    ENetEvent event;
    while (enet_host_service(client, &event, 0) > 0)
    {
      switch (event.type)
      {
      case ENET_EVENT_TYPE_CONNECT:
        printf("Connection with %x:%u established\n", event.peer->address.host, event.peer->address.port);
        send_join(serverPeer);
        connected = true;
        break;
      case ENET_EVENT_TYPE_RECEIVE:
        switch (get_packet_type(event.packet))
        {
        case E_SERVER_TO_CLIENT_NEW_ENTITY:
          on_new_entity_packet(event.packet);
          printf("new it\n");
          break;
        case E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY:
          on_set_controlled_entity(event.packet);
          printf("got it\n");
          break;
        case E_SERVER_TO_CLIENT_SNAPSHOT:
          on_snapshot(event.packet);
          break;
        case E_SERVER_TO_CLIENT_ENTITY_DEVOURED:
          on_entity_devoured(event.packet);
          break;
        case E_SERVER_TO_CLIENT_SCORE_UPDATE:
          on_score_update(event.packet);
          break;
        case E_SERVER_TO_CLIENT_GAME_TIME:
          on_game_time(event.packet);
          break;
        case E_SERVER_TO_CLIENT_GAME_OVER:
          on_game_over(event.packet);
          break;
        case E_CLIENT_TO_SERVER_JOIN:
          // No action needed for client-to-server packets on client
          break;
        case E_CLIENT_TO_SERVER_STATE:
          // No action needed for client-to-server packets on client
          break;
        };
        break;
      default:
        break;
      };
    }
    if (myEntity != INVALID_ENTITY)
    {
      bool left = IsKeyDown(KEY_LEFT);
      bool right = IsKeyDown(KEY_RIGHT);
      bool up = IsKeyDown(KEY_UP);
      bool down = IsKeyDown(KEY_DOWN);
      get_entity(myEntity, [&](Entity& e)
      {
        e.x += ((left ? -dt : 0.f) + (right ? +dt : 0.f)) * 100.f;
        e.y += ((up ? -dt : 0.f) + (down ? +dt : 0.f)) * 100.f;

        send_entity_state(serverPeer, myEntity, e.x, e.y);
        camera.target.x = e.x;
        camera.target.y = e.y;
      });
    }


    BeginDrawing();
      ClearBackground(Color{40, 40, 40, 255});
      BeginMode2D(camera);
        for (const Entity &e : entities)
        {
          DrawCircle((int)e.x, (int)e.y, e.size, GetColor(e.color));

          char idText[10];
          sprintf(idText, "%d", e.eid);
          DrawText(idText, (int)(e.x - 10), (int)(e.y - 10), 10, WHITE);
        }
      EndMode2D();

      if (myEntity != INVALID_ENTITY)
      {
        get_entity(myEntity, [&](Entity& e)
        {
          char scoreText[50];
          sprintf(scoreText, "Your Score: %d", e.score);
          DrawText(scoreText, 10, 10, 20, WHITE);

          char sizeText[50];
          sprintf(sizeText, "Size: %.1f", e.size);
          DrawText(sizeText, 10, 40, 20, WHITE);
        });
      }

      // Display game timer & Leaderboard
      char timeText[50];
      sprintf(timeText, "Time: %d", gameTimeRemaining);
      DrawText(timeText, width / 2 - 50, 10, 30, YELLOW);

      DrawRectangle(width - 200, 10, 190, 210, Color{0, 0, 0, 150});
      DrawText("LEADERBOARD", width - 190, 15, 20, YELLOW);

      std::vector<Entity> sortedEntities = entities;
      std::sort(sortedEntities.begin(), sortedEntities.end(), compare_entity_scores);

      int maxToShow = std::min(8, (int)sortedEntities.size());
      for (int i = 0; i < maxToShow; i++)
      {
        const Entity& e = sortedEntities[i];
        char playerText[100];
        const char* playerType = e.serverControlled ? "AI" : "Player";
        // Green of current player, white for others
        Color textColor = (e.eid == myEntity) ? GREEN : WHITE;

        sprintf(playerText, "%d. %s %d - Score: %d",
                i + 1,
                playerType,
                e.eid,
                e.score);

        DrawText(playerText, width - 190, 45 + (i * 20), 15, textColor);
      }

      if (gameOver)
      {
        DrawRectangle(0, 0, width, height, Color{0, 0, 0, 200});

        DrawText("GAME OVER", width/2 - 150, height/2 - 100, 50, RED);

        std::string winnerType = "Unknown";
        Color winnerColor = WHITE;

        get_entity(winnerEid, [&](Entity& e) {
          winnerType = e.serverControlled ? "AI" : "Player";
          winnerColor = GetColor(e.color);
        });

        char winnerText[100];
        sprintf(winnerText, "Winner: %s %d", winnerType.c_str(), winnerEid);
        DrawText(winnerText, width/2 - 120, height/2, 30, winnerColor);

        char scoreText[50];
        sprintf(scoreText, "Final Score: %d", winnerScore);
        DrawText(scoreText, width/2 - 100, height/2 + 50, 30, YELLOW);
      }
    EndDrawing();
  }

  CloseWindow();

  return 0;
}