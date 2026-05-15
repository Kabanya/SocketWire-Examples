#include "protocol.h"

#include "benchmark_utils.hpp"
#include "bit_stream.hpp"
#include "quantisation.h"
#include "reliable_connection.hpp"

#include <cstdint>

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
  bs.WriteBytes(&ent, sizeof(Entity));
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

void send_entity_input(socketwire::ReliableConnection* connection, std::uint16_t eid, float thr, float steer)
{
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_CLIENT_TO_SERVER_INPUT);
  bs.Write<std::uint16_t>(eid);

  const float4bitsQuantized thrPacked(thr, -1.f, 1.f);
  const float4bitsQuantized steerPacked(steer, -1.f, 1.f);
  const auto thrSteerPacked = static_cast<std::uint8_t>((thrPacked.packedVal << 4) | steerPacked.packedVal);
  bs.Write<std::uint8_t>(thrSteerPacked);

  if (connection->SendUnsequenced(1, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

using PositionXQuantized = PackedFloat<std::uint16_t, 11>;
using PositionYQuantized = PackedFloat<std::uint16_t, 10>;

void send_snapshot(socketwire::ReliableConnection* connection, std::uint16_t eid, float x, float y, float ori)
{
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_SERVER_TO_CLIENT_SNAPSHOT);
  bs.Write<std::uint16_t>(eid);

  const PositionXQuantized xPacked(x, -WORLD_SIZE, WORLD_SIZE);
  const PositionYQuantized yPacked(y, -WORLD_SIZE, WORLD_SIZE);
  const std::uint8_t oriPacked = pack_float<std::uint8_t>(ori, -PI, PI, 8);
  bs.Write<std::uint16_t>(xPacked.packedVal);
  bs.Write<std::uint16_t>(yPacked.packedVal);
  bs.Write<std::uint8_t>(oriPacked);

  if (connection->SendUnsequenced(1, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

void send_time_msec(socketwire::ReliableConnection* connection, std::uint32_t timeMsec)
{
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_SERVER_TO_CLIENT_TIME_MSEC);
  bs.Write<std::uint32_t>(timeMsec);
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
  bs.ReadBytes(&ent, sizeof(Entity));
}

void deserialize_set_controlled_entity(const void* data, std::size_t size, std::uint16_t& eid)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);
}

void deserialize_entity_input(const void* data, std::size_t size, std::uint16_t& eid, float& thr, float& steer)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);

  std::uint8_t thrSteerPacked = 0;
  bs.Read<std::uint8_t>(thrSteerPacked);

  static const std::uint8_t NEUTRAL_PACKED_VALUE = pack_float<std::uint8_t>(0.f, -1.f, 1.f, 4);
  float4bitsQuantized thrPacked(thrSteerPacked >> 4);
  float4bitsQuantized steerPacked(thrSteerPacked & 0x0f);
  thr = thrPacked.packedVal == NEUTRAL_PACKED_VALUE ? 0.f : thrPacked.unpack(-1.f, 1.f);
  steer = steerPacked.packedVal == NEUTRAL_PACKED_VALUE ? 0.f : steerPacked.unpack(-1.f, 1.f);
}

void deserialize_snapshot(const void* data, std::size_t size, std::uint16_t& eid, float& x, float& y, float& ori)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);

  std::uint16_t xPacked = 0;
  std::uint16_t yPacked = 0;
  std::uint8_t oriPacked = 0;
  bs.Read<std::uint16_t>(xPacked);
  bs.Read<std::uint16_t>(yPacked);
  bs.Read<std::uint8_t>(oriPacked);

  PositionXQuantized xPackedVal(xPacked);
  PositionYQuantized yPackedVal(yPacked);
  x = xPackedVal.unpack(-WORLD_SIZE, WORLD_SIZE);
  y = yPackedVal.unpack(-WORLD_SIZE, WORLD_SIZE);
  ori = unpack_float<std::uint8_t>(oriPacked, -PI, PI, 8);
}

void deserialize_time_msec(const void* data, std::size_t size, std::uint32_t& timeMsec)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint32_t>(timeMsec);
}
