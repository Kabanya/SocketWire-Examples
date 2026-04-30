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
  E_CLIENT_TO_SERVER_INPUT,
  E_SERVER_TO_CLIENT_SNAPSHOT,
  E_SERVER_TO_CLIENT_TIME_MSEC
};

void send_join(socketwire::ReliableConnection* connection);
void send_new_entity(socketwire::ReliableConnection* connection, const Entity& ent);
void send_set_controlled_entity(socketwire::ReliableConnection* connection, std::uint16_t eid);
void send_entity_input(socketwire::ReliableConnection* connection, std::uint16_t eid, float thr, float steer);
void send_snapshot(socketwire::ReliableConnection* connection, std::uint16_t eid, float x, float y, float ori);
void send_time_msec(socketwire::ReliableConnection* connection, std::uint32_t timeMsec);

MessageType get_packet_type(const void* data, std::size_t size);

void deserialize_new_entity(const void* data, std::size_t size, Entity& ent);
void deserialize_set_controlled_entity(const void* data, std::size_t size, std::uint16_t& eid);
void deserialize_entity_input(const void* data, std::size_t size, std::uint16_t& eid, float& thr, float& steer);
void deserialize_snapshot(const void* data, std::size_t size, std::uint16_t& eid, float& x, float& y, float& ori);
void deserialize_time_msec(const void* data, std::size_t size, std::uint32_t& timeMsec);
