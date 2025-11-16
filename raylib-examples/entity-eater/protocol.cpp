#include "protocol.h"
#include "bit_stream.hpp"
#include "reliable_connection.hpp"

void send_join(socketwire::ReliableConnection* connection)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_CLIENT_TO_SERVER_JOIN);

  connection->sendReliable(0, bs);
}

void send_new_entity(socketwire::ReliableConnection* connection, const Entity &ent)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_NEW_ENTITY);

  bs.write<uint32_t>(ent.color);
  bs.write<float>(ent.x);
  bs.write<float>(ent.y);
  bs.write<uint16_t>(ent.eid);
  bs.write<bool>(ent.serverControlled);
  bs.write<float>(ent.targetX);
  bs.write<float>(ent.targetY);
  bs.write<float>(ent.size);
  bs.write<int>(ent.score);

  connection->sendReliable(0, bs);
}

void send_set_controlled_entity(socketwire::ReliableConnection* connection, uint16_t eid)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY);
  bs.write<uint16_t>(eid);

  connection->sendReliable(0, bs);
}

void send_entity_state(socketwire::ReliableConnection* connection, uint16_t eid, float x, float y)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_CLIENT_TO_SERVER_STATE);
  bs.write<uint16_t>(eid);
  bs.write<float>(x);
  bs.write<float>(y);

  connection->sendUnsequenced(1, bs);
}

void send_snapshot(socketwire::ReliableConnection* connection, uint16_t eid, float x, float y, float size)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_SNAPSHOT);
  bs.write<uint16_t>(eid);
  bs.write<float>(x);
  bs.write<float>(y);
  bs.write<float>(size);

  connection->sendUnreliable(1, bs);
}

MessageType get_packet_type(const void* data, size_t size)
{
  if (size < 1)
    return E_CLIENT_TO_SERVER_JOIN; // fallback
  
  return static_cast<MessageType>(*static_cast<const uint8_t*>(data));
}

void deserialize_new_entity(const void* data, size_t size, Entity &ent)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);

  bs.read<uint32_t>(ent.color);
  bs.read<float>(ent.x);
  bs.read<float>(ent.y);
  bs.read<uint16_t>(ent.eid);
  bs.read<bool>(ent.serverControlled);
  bs.read<float>(ent.targetX);
  bs.read<float>(ent.targetY);
  bs.read<float>(ent.size);
  bs.read<int>(ent.score);
}

void deserialize_set_controlled_entity(const void* data, size_t size, uint16_t &eid)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(eid);
}

void deserialize_entity_state(const void* data, size_t size, uint16_t &eid, float &x, float &y)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(eid);
  bs.read<float>(x);
  bs.read<float>(y);
}

void deserialize_snapshot(const void* data, size_t size, uint16_t &eid, float &x, float &y, float &size_out)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(eid);
  bs.read<float>(x);
  bs.read<float>(y);
  bs.read<float>(size_out);
}

void send_entity_devoured(socketwire::ReliableConnection* connection, uint16_t devoured_eid, uint16_t devourer_eid, float new_size, float new_x, float new_y)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_ENTITY_DEVOURED);
  bs.write<uint16_t>(devoured_eid);
  bs.write<uint16_t>(devourer_eid);
  bs.write<float>(new_size);
  bs.write<float>(new_x);
  bs.write<float>(new_y);

  connection->sendReliable(0, bs);
}

void deserialize_entity_devoured(const void* data, size_t size, uint16_t &devoured_eid, uint16_t &devourer_eid, float &new_size, float &new_x, float &new_y)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(devoured_eid);
  bs.read<uint16_t>(devourer_eid);
  bs.read<float>(new_size);
  bs.read<float>(new_x);
  bs.read<float>(new_y);
}

void send_score_update(socketwire::ReliableConnection* connection, uint16_t eid, int score)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_SCORE_UPDATE);
  bs.write<uint16_t>(eid);
  bs.write<int>(score);

  connection->sendReliable(0, bs);
}

void deserialize_score_update(const void* data, size_t size, uint16_t &eid, int &score)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(eid);
  bs.read<int>(score);
}

void send_game_time(socketwire::ReliableConnection* connection, int seconds_remaining)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_GAME_TIME);
  bs.write<int>(seconds_remaining);

  connection->sendReliable(0, bs);
}

void send_game_over(socketwire::ReliableConnection* connection, uint16_t winner_eid, int winner_score)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_GAME_OVER);
  bs.write<uint16_t>(winner_eid);
  bs.write<int>(winner_score);

  connection->sendReliable(0, bs);
}

void deserialize_game_over(const void* data, size_t size, uint16_t &winner_eid, int &winner_score)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(winner_eid);
  bs.read<int>(winner_score);
}

void deserialize_game_time(const void* data, size_t size, int &seconds_remaining)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<int>(seconds_remaining);
}