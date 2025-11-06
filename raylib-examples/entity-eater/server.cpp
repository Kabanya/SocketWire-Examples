#include <enet/enet.h>
// #include <iostream>
#include "entity.h"
#include "protocol.h"
#include <stdlib.h>
#include <vector>
#include <map>
#include <stdio.h>
#include <cmath>
#include <algorithm>

static std::vector<Entity> entities;
static std::map<uint16_t, ENetPeer*> controlledMap;

float random_spawn(const float max_size = 10.f)
{
  return (rand() % 100 - 50) * max_size;
}

static uint16_t create_random_entity()
{
  uint16_t newEid = entities.size();
  uint32_t color = 0xff000000 +
                   0x00440000 * (1 + rand() % 4) +
                   0x00004400 * (1 + rand() % 4) +
                   0x00000044 * (1 + rand() % 4);
  float x = random_spawn();
  float y = random_spawn();
  float size = 5.f + (rand() % 6);   // Random size between 5 and 10

  Entity ent;
  ent.color = color;
  ent.x = x;
  ent.y = y;
  ent.eid = newEid;
  ent.serverControlled = false;
  ent.targetX = 0.f;
  ent.targetY = 0.f;
  ent.size = size;
  ent.score = 0;

  entities.push_back(ent);
  return newEid;
}

void on_join([[maybe_unused]]ENetPacket *packet, ENetPeer *peer, ENetHost *host)
{
  // send all entities
  for (const Entity &ent : entities)
    send_new_entity(peer, ent);

  // find max eid
  uint16_t newEid = create_random_entity();
  const Entity& ent = entities[newEid];

  controlledMap[newEid] = peer;


  // send info about new entity to everyone
  for (size_t i = 0; i < host->peerCount; ++i)
    send_new_entity(&host->peers[i], ent);
  // send info about controlled entity
  send_set_controlled_entity(peer, newEid);
}

void on_state(ENetPacket *packet)
{
  uint16_t eid = INVALID_ENTITY;
  float x = 0.f; float y = 0.f;
  deserialize_entity_state(packet, eid, x, y);
  for (Entity &e : entities)
    if (e.eid == eid)
    {
      e.x = x;
      e.y = y;
    }
}

// int main(int argc, const char **argv)
int main()
{
  if (enet_initialize() != 0)
  {
    printf("Cannot init ENet");
    return 1;
  }
  ENetAddress address;

  address.host = ENET_HOST_ANY;
  address.port = 10131;

  ENetHost *server = enet_host_create(&address, 32, 2, 0, 0);

  if (server == nullptr)
  {
    printf("Cannot create ENet server\n");
    return 1;
  }

  bool created_ai_entities = false;
  constexpr int NUM_AI = 10;

  const int GAME_DURATION = 60; // game timer
  int game_time_remaining = GAME_DURATION;
  uint32_t last_time_update = 0;
  bool game_over = false;

  uint32_t lastTime = enet_time_get();
  while (true)
  {
    uint32_t curTime = enet_time_get();
    float dt = (curTime - lastTime) * 0.001f;
    lastTime = curTime;

    if (!game_over && created_ai_entities && curTime - last_time_update >= 1000) {
      game_time_remaining--;
      last_time_update = curTime;

      // Send time updates to all clients
      for (size_t i = 0; i < server->peerCount; ++i) {
        ENetPeer *peer = &server->peers[i];
        send_game_time(peer, game_time_remaining);
      }

      printf("Game time remaining: %d seconds\n", game_time_remaining);

      if (game_time_remaining <= 0) {
        game_over = true;

        // Find highest score
        uint16_t winner_eid = INVALID_ENTITY;
        int highest_score = -1;

        for (const Entity &e : entities) {
          if (e.score > highest_score) {
            highest_score = e.score;
            winner_eid = e.eid;
          }
        }

        printf("Game over! Winner is entity %d with score %d\n", 
               winner_eid, highest_score);

        for (size_t i = 0; i < server->peerCount; ++i) {
          ENetPeer *peer = &server->peers[i];
          send_game_over(peer, winner_eid, highest_score);
        }
      }
    }

    ENetEvent event;
    while (enet_host_service(server, &event, 0) > 0)
    {
      switch (event.type)
      {
      case ENET_EVENT_TYPE_CONNECT:
        printf("Connection with %x:%u established\n", event.peer->address.host, event.peer->address.port);

        if (!created_ai_entities) {
          printf("Creating AI entities for first client\n");
          for (int i = 0; i < NUM_AI; ++i)
          {
            uint16_t eid = create_random_entity();
            entities[eid].serverControlled = true;
            entities[eid].score = 0;
            controlledMap[eid] = nullptr;
          }
          created_ai_entities = true;
        }
        break;
      case ENET_EVENT_TYPE_RECEIVE:
        switch (get_packet_type(event.packet))
        {
          case E_CLIENT_TO_SERVER_JOIN:
            on_join(event.packet, event.peer, server);
            break;
          case E_CLIENT_TO_SERVER_STATE:
            on_state(event.packet);
            break;
          case E_SERVER_TO_CLIENT_NEW_ENTITY:
          case E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY:
          case E_SERVER_TO_CLIENT_SNAPSHOT:
          case E_SERVER_TO_CLIENT_ENTITY_DEVOURED:
          case E_SERVER_TO_CLIENT_SCORE_UPDATE:
          case E_SERVER_TO_CLIENT_GAME_TIME:
          case E_SERVER_TO_CLIENT_GAME_OVER:
            printf("Warning: Received server-to-client message on server\n");
            break;
        };
        enet_packet_destroy(event.packet);
        break;
      default:
        break;
      };
    }
    for (Entity &e : entities)
    {
      if (e.serverControlled)
      {
        const float diffX = e.targetX - e.x;
        const float diffY = e.targetY - e.y;
        const float dirX = diffX > 0.f ? 1.f : -1.f;
        const float dirY = diffY > 0.f ? 1.f : -1.f;
        constexpr float SPD = 50.f;
        e.x += dirX * SPD * dt;
        e.y += dirY * SPD * dt;
        if (fabsf(diffX) < 10.f && fabsf(diffY) < 10.f)
        {
          e.targetX = random_spawn();
          e.targetY = random_spawn();
        }
      }
    }

    bool collision_occurred = false;
    for (size_t i = 0; i < entities.size() && !collision_occurred; i++)
    {
      for (size_t j = 0; j < entities.size(); j++)
      {
        if (i == j) continue;

        Entity &e1 = entities[i];
        Entity &e2 = entities[j];

        if (e1.size <= 0 || e2.size <= 0 || e1.size > 1000 || e2.size > 1000) {
          continue;
        }

        float dx = e1.x - e2.x;
        float dy = e1.y - e2.y;
        float distance = sqrt(dx*dx + dy*dy);

        if (distance < (e1.size + e2.size) && e1.size != e2.size && distance > 0.1f)
        {
          printf("Collision detected between entities %d (size %.1f) and %d (size %.1f)! Distance: %.1f < %.1f\n", 
                 e1.eid, e1.size, e2.eid, e2.size, distance, (e1.size + e2.size));

          Entity *devourer = nullptr;
          Entity *devoured = nullptr;

          if (e1.size > e2.size)
          {
            devourer = &e1;
            devoured = &e2;
          }
          else
          {
            devourer = &e2;
            devoured = &e1;
          }

          printf("Entity %d (size %.1f) devours Entity %d (size %.1f)\n",
                 devourer->eid, devourer->size, devoured->eid, devoured->size);

          float size_gain = devoured->size / 2.0f;

          if (size_gain > 0.0f && size_gain < 50.0f) {
            const float MAX_SIZE = 100.0f;
            float newSize = devourer->size + size_gain;
            devourer->size = std::min(newSize, MAX_SIZE);

            devoured->size = 5.0f + (rand() % 5); // Random size between 5 and 10

            if (!devoured->serverControlled) {
              devoured->score = 0;
            }

            if (!devourer->serverControlled)
            {
              devourer->score += static_cast<int>(size_gain);
            }
            else
            {
              devourer->score += static_cast<int>(size_gain);
            }

            for (size_t k = 0; k < server->peerCount; ++k)
            {
              ENetPeer *peer = &server->peers[k];
              send_score_update(peer, devourer->eid, devourer->score);
            }

            devoured->x = (rand() % 100 - 50) * 10.f;
            devoured->y = (rand() % 100 - 50) * 10.f;

            for (size_t k = 0; k < server->peerCount; ++k)
            {
              ENetPeer *peer = &server->peers[k];
              send_entity_devoured(peer, devoured->eid, devourer->eid, 
                                  devourer->size, devoured->x, devoured->y);
            }

            collision_occurred = true;
          } else {
            printf("Warning: Invalid size gain (%.1f) detected! Skipping this collision.\n", size_gain);
          }
          break;
        }
      }
    }

    for (const Entity &e : entities)
    {
      for (size_t i = 0; i < server->peerCount; ++i)
      {
        ENetPeer *peer = &server->peers[i];
        if (controlledMap[e.eid] != peer)
          send_snapshot(peer, e.eid, e.x, e.y, e.size);
      }
    }
    //usleep(400000);
  }

  enet_host_destroy(server);

  atexit(enet_deinitialize);
  return 0;
}