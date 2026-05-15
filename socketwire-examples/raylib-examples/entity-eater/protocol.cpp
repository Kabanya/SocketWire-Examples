#include "protocol.h"

#include "benchmark_utils.hpp"
#include "bit_stream.hpp"
#include "reliable_connection.hpp"

void send_join(socketwire::ReliableConnection* connection)
{
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_CLIENT_TO_SERVER_JOIN);
  if (connection->SendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

void send_new_entity(socketwire::ReliableConnection* connection, const Entity& ent)
{
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_SERVER_TO_CLIENT_NEW_ENTITY);
  bs.Write<std::uint32_t>(ent.color);
  bs.Write<float>(ent.x);
  bs.Write<float>(ent.y);
  bs.Write<std::uint16_t>(ent.eid);
  bs.Write<bool>(ent.serverControlled);
  bs.Write<float>(ent.targetX);
  bs.Write<float>(ent.targetY);
  bs.Write<float>(ent.size);
  bs.Write<int>(ent.score);
  if (connection->SendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

void send_set_controlled_entity(socketwire::ReliableConnection* connection, std::uint16_t eid)
{
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY);
  bs.Write<std::uint16_t>(eid);
  if (connection->SendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

void send_entity_state(socketwire::ReliableConnection* connection, std::uint16_t eid, float x, float y)
{
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_CLIENT_TO_SERVER_STATE);
  bs.Write<std::uint16_t>(eid);
  bs.Write<float>(x);
  bs.Write<float>(y);
  if (connection->SendUnsequenced(1, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

void send_snapshot(socketwire::ReliableConnection* connection, std::uint16_t eid, float x, float y, float size)
{
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_SERVER_TO_CLIENT_SNAPSHOT);
  bs.Write<std::uint16_t>(eid);
  bs.Write<float>(x);
  bs.Write<float>(y);
  bs.Write<float>(size);
  if (connection->SendUnsequenced(1, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

void send_entity_devoured(socketwire::ReliableConnection* connection,
                          std::uint16_t devouredEid,
                          std::uint16_t devourerEid,
                          float newSize,
                          float newX,
                          float newY)
{
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_SERVER_TO_CLIENT_ENTITY_DEVOURED);
  bs.Write<std::uint16_t>(devouredEid);
  bs.Write<std::uint16_t>(devourerEid);
  bs.Write<float>(newSize);
  bs.Write<float>(newX);
  bs.Write<float>(newY);
  if (connection->SendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

void send_score_update(socketwire::ReliableConnection* connection, std::uint16_t eid, int score)
{
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_SERVER_TO_CLIENT_SCORE_UPDATE);
  bs.Write<std::uint16_t>(eid);
  bs.Write<int>(score);
  if (connection->SendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

void send_game_time(socketwire::ReliableConnection* connection, int secondsRemaining)
{
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_SERVER_TO_CLIENT_GAME_TIME);
  bs.Write<int>(secondsRemaining);
  if (connection->SendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

void send_game_over(socketwire::ReliableConnection* connection, std::uint16_t winnerEid, int winnerScore)
{
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_SERVER_TO_CLIENT_GAME_OVER);
  bs.Write<std::uint16_t>(winnerEid);
  bs.Write<int>(winnerScore);
  if (connection->SendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
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
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint32_t>(ent.color);
  bs.Read<float>(ent.x);
  bs.Read<float>(ent.y);
  bs.Read<std::uint16_t>(ent.eid);
  bs.Read<bool>(ent.serverControlled);
  bs.Read<float>(ent.targetX);
  bs.Read<float>(ent.targetY);
  bs.Read<float>(ent.size);
  bs.Read<int>(ent.score);
}

void deserialize_set_controlled_entity(const void* data, std::size_t size, std::uint16_t& eid)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);
}

void deserialize_entity_state(const void* data, std::size_t size, std::uint16_t& eid, float& x, float& y)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);
  bs.Read<float>(x);
  bs.Read<float>(y);
}

void deserialize_snapshot(const void* data, std::size_t size, std::uint16_t& eid, float& x, float& y, float& sizeOut)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);
  bs.Read<float>(x);
  bs.Read<float>(y);
  bs.Read<float>(sizeOut);
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
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(devouredEid);
  bs.Read<std::uint16_t>(devourerEid);
  bs.Read<float>(newSize);
  bs.Read<float>(newX);
  bs.Read<float>(newY);
}

void deserialize_score_update(const void* data, std::size_t size, std::uint16_t& eid, int& score)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);
  bs.Read<int>(score);
}

void deserialize_game_over(const void* data, std::size_t size, std::uint16_t& winnerEid, int& winnerScore)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(winnerEid);
  bs.Read<int>(winnerScore);
}

void deserialize_game_time(const void* data, std::size_t size, int& secondsRemaining)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<int>(secondsRemaining);
}
