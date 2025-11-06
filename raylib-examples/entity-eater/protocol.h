#pragma once
#include <cstdint>
#include <enet/enet.h>
#include "entity.h"

enum MessageType : uint8_t
{
  E_CLIENT_TO_SERVER_JOIN = 0,
  E_SERVER_TO_CLIENT_NEW_ENTITY,
  E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY,
  E_CLIENT_TO_SERVER_STATE,
  E_SERVER_TO_CLIENT_SNAPSHOT,
  E_SERVER_TO_CLIENT_ENTITY_DEVOURED,
  E_SERVER_TO_CLIENT_SCORE_UPDATE,
  E_SERVER_TO_CLIENT_GAME_TIME,
  E_SERVER_TO_CLIENT_GAME_OVER
};

void send_join(ENetPeer *peer);
void send_new_entity(ENetPeer *peer, const Entity &ent);
void send_set_controlled_entity(ENetPeer *peer, uint16_t eid);
void send_entity_state(ENetPeer *peer, uint16_t eid, float x, float y);
void send_snapshot(ENetPeer *peer, uint16_t eid, float x, float y, float size);

void send_entity_devoured(ENetPeer *peer, uint16_t devoured_eid, uint16_t devourer_eid, float new_size, float new_x, float new_y);
void send_score_update(ENetPeer *peer, uint16_t eid, int score);
void send_game_over(ENetPeer *peer, uint16_t winner_eid, int winner_score);
void send_game_time(ENetPeer *peer, int seconds_remaining);

MessageType get_packet_type(ENetPacket *packet);

void deserialize_new_entity(ENetPacket *packet, Entity &ent);
void deserialize_set_controlled_entity(ENetPacket *packet, uint16_t &eid);
void deserialize_entity_state(ENetPacket *packet, uint16_t &eid, float &x, float &y);
void deserialize_snapshot(ENetPacket *packet, uint16_t &eid, float &x, float &y, float &size);

void deserialize_score_update(ENetPacket *packet, uint16_t &eid, int &score);
void deserialize_entity_devoured(ENetPacket *packet, uint16_t &devoured_eid, uint16_t &devourer_eid, float &new_size, float &new_x, float &new_y);
void deserialize_game_over(ENetPacket *packet, uint16_t &winner_eid, int &winner_score);
void deserialize_game_time(ENetPacket *packet, int &seconds_remaining);