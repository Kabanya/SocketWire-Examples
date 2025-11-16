#pragma once
#include <cstdint>
#include <cstddef>
#include "entity.h"

// Forward declarations for SocketWire
namespace socketwire {
  class ReliableConnection;
  class BitStream;
}

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

void send_join(socketwire::ReliableConnection* connection);
void send_new_entity(socketwire::ReliableConnection* connection, const Entity &ent);
void send_set_controlled_entity(socketwire::ReliableConnection* connection, uint16_t eid);
void send_entity_state(socketwire::ReliableConnection* connection, uint16_t eid, float x, float y);
void send_snapshot(socketwire::ReliableConnection* connection, uint16_t eid, float x, float y, float size);

void send_entity_devoured(socketwire::ReliableConnection* connection, uint16_t devoured_eid, uint16_t devourer_eid, float new_size, float new_x, float new_y);
void send_score_update(socketwire::ReliableConnection* connection, uint16_t eid, int score);
void send_game_over(socketwire::ReliableConnection* connection, uint16_t winner_eid, int winner_score);
void send_game_time(socketwire::ReliableConnection* connection, int seconds_remaining);

MessageType get_packet_type(const void* data, size_t size);

void deserialize_new_entity(const void* data, size_t size, Entity &ent);
void deserialize_set_controlled_entity(const void* data, size_t size, uint16_t &eid);
void deserialize_entity_state(const void* data, size_t size, uint16_t &eid, float &x, float &y);
void deserialize_snapshot(const void* data, size_t size, uint16_t &eid, float &x, float &y, float &size_out);

void deserialize_score_update(const void* data, size_t size, uint16_t &eid, int &score);
void deserialize_entity_devoured(const void* data, size_t size, uint16_t &devoured_eid, uint16_t &devourer_eid, float &new_size, float &new_x, float &new_y);
void deserialize_game_over(const void* data, size_t size, uint16_t &winner_eid, int &winner_score);
void deserialize_game_time(const void* data, size_t size, int &seconds_remaining);