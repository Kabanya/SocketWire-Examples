#include "protocol.h"

#include "benchmark_utils.hpp"
#include "bit_stream.hpp"
#include "reliable_connection.hpp"

void send_join(socketwire::ReliableConnection* connection)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_CLIENT_TO_SERVER_JOIN);
  if (connection->sendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.getSizeBytes());
}

void send_new_entity(socketwire::ReliableConnection* connection, const Entity& ent)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_SERVER_TO_CLIENT_NEW_ENTITY);
  bs.write<std::uint32_t>(ent.color);
  bs.write<float>(ent.x);
  bs.write<float>(ent.y);
  bs.write<std::uint16_t>(ent.eid);
  bs.write<bool>(ent.serverControlled);
  bs.write<float>(ent.targetX);
  bs.write<float>(ent.targetY);
  bs.write<float>(ent.size);
  bs.write<int>(ent.score);
  if (connection->sendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.getSizeBytes());
}

void send_set_controlled_entity(socketwire::ReliableConnection* connection, std::uint16_t eid)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY);
  bs.write<std::uint16_t>(eid);
  if (connection->sendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.getSizeBytes());
}

void send_entity_state(socketwire::ReliableConnection* connection, std::uint16_t eid, float x, float y)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_CLIENT_TO_SERVER_STATE);
  bs.write<std::uint16_t>(eid);
  bs.write<float>(x);
  bs.write<float>(y);
  if (connection->sendUnsequenced(1, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.getSizeBytes());
}

void send_snapshot(socketwire::ReliableConnection* connection, std::uint16_t eid, float x, float y, float size)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_SERVER_TO_CLIENT_SNAPSHOT);
  bs.write<std::uint16_t>(eid);
  bs.write<float>(x);
  bs.write<float>(y);
  bs.write<float>(size);
  if (connection->sendUnsequenced(1, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.getSizeBytes());
}

void send_entity_devoured(socketwire::ReliableConnection* connection,
                          std::uint16_t devouredEid,
                          std::uint16_t devourerEid,
                          float newSize,
                          float newX,
                          float newY)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_SERVER_TO_CLIENT_ENTITY_DEVOURED);
  bs.write<std::uint16_t>(devouredEid);
  bs.write<std::uint16_t>(devourerEid);
  bs.write<float>(newSize);
  bs.write<float>(newX);
  bs.write<float>(newY);
  if (connection->sendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.getSizeBytes());
}

void send_score_update(socketwire::ReliableConnection* connection, std::uint16_t eid, int score)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_SERVER_TO_CLIENT_SCORE_UPDATE);
  bs.write<std::uint16_t>(eid);
  bs.write<int>(score);
  if (connection->sendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.getSizeBytes());
}

void send_game_time(socketwire::ReliableConnection* connection, int secondsRemaining)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_SERVER_TO_CLIENT_GAME_TIME);
  bs.write<int>(secondsRemaining);
  if (connection->sendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.getSizeBytes());
}

void send_game_over(socketwire::ReliableConnection* connection, std::uint16_t winnerEid, int winnerScore)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_SERVER_TO_CLIENT_GAME_OVER);
  bs.write<std::uint16_t>(winnerEid);
  bs.write<int>(winnerScore);
  if (connection->sendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.getSizeBytes());
}

MessageType get_packet_type(const void* data, std::size_t size)
{
  if (data == nullptr || size < 1)
    return E_CLIENT_TO_SERVER_JOIN;
  return static_cast<MessageType>(*static_cast<const std::uint8_t*>(data));
}

void deserialize_new_entity(const void* data, std::size_t size, Entity& ent)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.read<std::uint8_t>(type);
  bs.read<std::uint32_t>(ent.color);
  bs.read<float>(ent.x);
  bs.read<float>(ent.y);
  bs.read<std::uint16_t>(ent.eid);
  bs.read<bool>(ent.serverControlled);
  bs.read<float>(ent.targetX);
  bs.read<float>(ent.targetY);
  bs.read<float>(ent.size);
  bs.read<int>(ent.score);
}

void deserialize_set_controlled_entity(const void* data, std::size_t size, std::uint16_t& eid)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.read<std::uint8_t>(type);
  bs.read<std::uint16_t>(eid);
}

void deserialize_entity_state(const void* data, std::size_t size, std::uint16_t& eid, float& x, float& y)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.read<std::uint8_t>(type);
  bs.read<std::uint16_t>(eid);
  bs.read<float>(x);
  bs.read<float>(y);
}

void deserialize_snapshot(const void* data, std::size_t size, std::uint16_t& eid, float& x, float& y, float& sizeOut)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.read<std::uint8_t>(type);
  bs.read<std::uint16_t>(eid);
  bs.read<float>(x);
  bs.read<float>(y);
  bs.read<float>(sizeOut);
}

void deserialize_entity_devoured(const void* data,
                                 std::size_t size,
                                 std::uint16_t& devouredEid,
                                 std::uint16_t& devourerEid,
                                 float& newSize,
                                 float& newX,
                                 float& newY)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.read<std::uint8_t>(type);
  bs.read<std::uint16_t>(devouredEid);
  bs.read<std::uint16_t>(devourerEid);
  bs.read<float>(newSize);
  bs.read<float>(newX);
  bs.read<float>(newY);
}

void deserialize_score_update(const void* data, std::size_t size, std::uint16_t& eid, int& score)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.read<std::uint8_t>(type);
  bs.read<std::uint16_t>(eid);
  bs.read<int>(score);
}

void deserialize_game_over(const void* data, std::size_t size, std::uint16_t& winnerEid, int& winnerScore)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.read<std::uint8_t>(type);
  bs.read<std::uint16_t>(winnerEid);
  bs.read<int>(winnerScore);
}

void deserialize_game_time(const void* data, std::size_t size, int& secondsRemaining)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.read<std::uint8_t>(type);
  bs.read<int>(secondsRemaining);
}
