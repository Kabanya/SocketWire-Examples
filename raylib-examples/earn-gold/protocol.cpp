#include "protocol.h"
#include "bit_stream.hpp"
#include "reliable_connection.hpp"
#include <cstdio>

MessageType get_packet_type(const void* data, size_t size)
{
  if (size < 1)
    return E_CLIENT_TO_SERVER_JOIN; // fallback
  return static_cast<MessageType>(*static_cast<const uint8_t*>(data));
}

// Client -> Server
void send_join(socketwire::ReliableConnection* connection)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_CLIENT_TO_SERVER_JOIN);
  connection->sendReliable(0, bs);
}

void send_player_move(socketwire::ReliableConnection* connection, float x, float y)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_CLIENT_TO_SERVER_MOVE);
  bs.write<float>(x);
  bs.write<float>(y);
  connection->sendUnsequenced(1, bs);
}

// Server -> Client
void send_welcome(socketwire::ReliableConnection* connection, uint16_t player_id)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_WELCOME);
  bs.write<uint16_t>(player_id);
  connection->sendReliable(0, bs);
}

void send_player_joined(socketwire::ReliableConnection* connection, uint16_t player_id, float x, float y, uint32_t color)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_PLAYER_JOINED);
  bs.write<uint16_t>(player_id);
  bs.write<float>(x);
  bs.write<float>(y);
  bs.write<uint32_t>(color);
  connection->sendReliable(0, bs);
}

void send_player_left(socketwire::ReliableConnection* connection, uint16_t player_id)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_PLAYER_LEFT);
  bs.write<uint16_t>(player_id);
  connection->sendReliable(0, bs);
}

void send_player_update(socketwire::ReliableConnection* connection, uint16_t player_id, float x, float y)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_PLAYER_UPDATE);
  bs.write<uint16_t>(player_id);
  bs.write<float>(x);
  bs.write<float>(y);
  connection->sendUnreliable(1, bs);
}

void send_coin_spawn(socketwire::ReliableConnection* connection, uint16_t coin_id, float x, float y)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_COIN_SPAWN);
  bs.write<uint16_t>(coin_id);
  bs.write<float>(x);
  bs.write<float>(y);
  connection->sendReliable(0, bs);
}

void send_coin_collected(socketwire::ReliableConnection* connection, uint16_t coin_id, uint16_t player_id)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_COIN_COLLECTED);
  bs.write<uint16_t>(coin_id);
  bs.write<uint16_t>(player_id);
  connection->sendReliable(0, bs);
}

void send_score_update(socketwire::ReliableConnection* connection, uint16_t player_id, int score)
{
  socketwire::BitStream bs;
  bs.write<uint8_t>(E_SERVER_TO_CLIENT_SCORE_UPDATE);
  bs.write<uint16_t>(player_id);
  bs.write<int>(score);
  connection->sendReliable(0, bs);
}

// Deserialization
void deserialize_welcome(const void* data, size_t size, uint16_t& player_id)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(player_id);
}

void deserialize_player_joined(const void* data, size_t size, uint16_t& player_id, float& x, float& y, uint32_t& color)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(player_id);
  bs.read<float>(x);
  bs.read<float>(y);
  bs.read<uint32_t>(color);
}

void deserialize_player_left(const void* data, size_t size, uint16_t& player_id)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(player_id);
}

void deserialize_player_update(const void* data, size_t size, uint16_t& player_id, float& x, float& y)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(player_id);
  bs.read<float>(x);
  bs.read<float>(y);
}

void deserialize_player_move(const void* data, size_t size, float& x, float& y)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<float>(x);
  bs.read<float>(y);
}

void deserialize_coin_spawn(const void* data, size_t size, uint16_t& coin_id, float& x, float& y)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(coin_id);
  bs.read<float>(x);
  bs.read<float>(y);
}

void deserialize_coin_collected(const void* data, size_t size, uint16_t& coin_id, uint16_t& player_id)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(coin_id);
  bs.read<uint16_t>(player_id);
}

void deserialize_score_update(const void* data, size_t size, uint16_t& player_id, int& score)
{
  socketwire::BitStream bs(static_cast<const uint8_t*>(data), size);
  uint8_t type;
  bs.read<uint8_t>(type);
  bs.read<uint16_t>(player_id);
  bs.read<int>(score);
}