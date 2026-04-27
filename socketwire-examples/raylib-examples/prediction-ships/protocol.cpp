#include "protocol.h"

#include "bit_stream.hpp"
#include "reliable_connection.hpp"

void send_join(socketwire::ReliableConnection* connection)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_CLIENT_TO_SERVER_JOIN);
  connection->sendReliable(0, bs);
}

void send_new_entity(socketwire::ReliableConnection* connection, const Entity& ent)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_SERVER_TO_CLIENT_NEW_ENTITY);
  bs.write<std::uint32_t>(ent.color);
  bs.write<float>(ent.x);
  bs.write<float>(ent.y);
  bs.write<std::uint16_t>(ent.eid);
  bs.write<float>(ent.vx);
  bs.write<float>(ent.vy);
  bs.write<float>(ent.ori);
  bs.write<float>(ent.omega);
  bs.write<float>(ent.thr);
  bs.write<float>(ent.steer);
  bs.write<std::uint16_t>(ent.eid);
  connection->sendReliable(0, bs);
}

void send_set_controlled_entity(socketwire::ReliableConnection* connection, std::uint16_t eid)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY);
  bs.write<std::uint16_t>(eid);
  connection->sendReliable(0, bs);
}

void send_entity_input(socketwire::ReliableConnection* connection, std::uint16_t eid, float thr, float steer)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_CLIENT_TO_SERVER_INPUT);
  bs.write<std::uint16_t>(eid);
  bs.write<float>(thr);
  bs.write<float>(steer);
  connection->sendUnsequenced(1, bs);
}

void send_snapshot(socketwire::ReliableConnection* connection,
                   std::uint16_t eid,
                   float x,
                   float y,
                   float ori,
                   float vx,
                   float vy,
                   float omega,
                   TimePoint timestamp,
                   std::uint32_t frameNumber)
{
  const auto duration = timestamp.time_since_epoch();
  const auto timestampMs =
    static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());

  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_SERVER_TO_CLIENT_SNAPSHOT);
  bs.write<std::uint16_t>(eid);
  bs.write<float>(x);
  bs.write<float>(y);
  bs.write<float>(ori);
  bs.write<float>(vx);
  bs.write<float>(vy);
  bs.write<float>(omega);
  bs.write<std::uint64_t>(timestampMs);
  bs.write<std::uint32_t>(frameNumber);
  connection->sendUnsequenced(1, bs);
}

void send_time_msec(socketwire::ReliableConnection* connection, std::uint32_t timeMsec)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_SERVER_TO_CLIENT_TIME_MSEC);
  bs.write<std::uint32_t>(timeMsec);
  connection->sendReliable(0, bs);
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
  bs.read<float>(ent.vx);
  bs.read<float>(ent.vy);
  bs.read<float>(ent.ori);
  bs.read<float>(ent.omega);
  bs.read<float>(ent.thr);
  bs.read<float>(ent.steer);
  bs.read<std::uint16_t>(ent.eid);
}

void deserialize_set_controlled_entity(const void* data, std::size_t size, std::uint16_t& eid)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.read<std::uint8_t>(type);
  bs.read<std::uint16_t>(eid);
}

void deserialize_entity_input(const void* data, std::size_t size, std::uint16_t& eid, float& thr, float& steer)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.read<std::uint8_t>(type);
  bs.read<std::uint16_t>(eid);
  bs.read<float>(thr);
  bs.read<float>(steer);
}

void deserialize_snapshot(const void* data,
                          std::size_t size,
                          std::uint16_t& eid,
                          float& x,
                          float& y,
                          float& ori,
                          float& vx,
                          float& vy,
                          float& omega,
                          TimePoint& timestamp,
                          std::uint32_t& frameNumber)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.read<std::uint8_t>(type);
  bs.read<std::uint16_t>(eid);
  bs.read<float>(x);
  bs.read<float>(y);
  bs.read<float>(ori);
  bs.read<float>(vx);
  bs.read<float>(vy);
  bs.read<float>(omega);

  std::uint64_t timestampMs = 0;
  bs.read<std::uint64_t>(timestampMs);
  bs.read<std::uint32_t>(frameNumber);
  timestamp = TimePoint(std::chrono::milliseconds(timestampMs));
}

void deserialize_time_msec(const void* data, std::size_t size, std::uint32_t& timeMsec)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.read<std::uint8_t>(type);
  bs.read<std::uint32_t>(timeMsec);
}
