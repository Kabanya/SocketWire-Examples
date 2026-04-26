#pragma once

#include "entity.h"

#include <cstddef>
#include <cstdint>

namespace socketwire
{
class ReliableConnection;
}

enum MessageType : std::uint8_t
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
void send_new_entity(socketwire::ReliableConnection* connection, const Entity& ent);
void send_set_controlled_entity(socketwire::ReliableConnection* connection, std::uint16_t eid);
void send_entity_state(socketwire::ReliableConnection* connection, std::uint16_t eid, float x, float y);
void send_snapshot(socketwire::ReliableConnection* connection, std::uint16_t eid, float x, float y, float size);

void send_entity_devoured(socketwire::ReliableConnection* connection,
                          std::uint16_t devouredEid,
                          std::uint16_t devourerEid,
                          float newSize,
                          float newX,
                          float newY);
void send_score_update(socketwire::ReliableConnection* connection, std::uint16_t eid, int score);
void send_game_over(socketwire::ReliableConnection* connection, std::uint16_t winnerEid, int winnerScore);
void send_game_time(socketwire::ReliableConnection* connection, int secondsRemaining);

MessageType get_packet_type(const void* data, std::size_t size);

void deserialize_new_entity(const void* data, std::size_t size, Entity& ent);
void deserialize_set_controlled_entity(const void* data, std::size_t size, std::uint16_t& eid);
void deserialize_entity_state(const void* data, std::size_t size, std::uint16_t& eid, float& x, float& y);
void deserialize_snapshot(const void* data, std::size_t size, std::uint16_t& eid, float& x, float& y, float& sizeOut);

void deserialize_score_update(const void* data, std::size_t size, std::uint16_t& eid, int& score);
void deserialize_entity_devoured(const void* data,
                                 std::size_t size,
                                 std::uint16_t& devouredEid,
                                 std::uint16_t& devourerEid,
                                 float& newSize,
                                 float& newX,
                                 float& newY);
void deserialize_game_over(const void* data, std::size_t size, std::uint16_t& winnerEid, int& winnerScore);
void deserialize_game_time(const void* data, std::size_t size, int& secondsRemaining);
