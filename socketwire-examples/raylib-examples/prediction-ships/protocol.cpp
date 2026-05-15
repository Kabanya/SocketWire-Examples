#include "protocol.h"

#include "benchmark_utils.hpp"
#include "bit_stream.hpp"
#include "reliable_connection.hpp"

void send_join(socketwire::ReliableConnection* connection) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_CLIENT_TO_SERVER_JOIN);
  if (connection->SendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

void send_new_entity(socketwire::ReliableConnection* connection,
                     const Entity& ent) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_SERVER_TO_CLIENT_NEW_ENTITY);
  bs.Write<std::uint32_t>(ent.color);
  bs.Write<float>(ent.x);
  bs.Write<float>(ent.y);
  bs.Write<std::uint16_t>(ent.eid);
  bs.Write<float>(ent.vx);
  bs.Write<float>(ent.vy);
  bs.Write<float>(ent.ori);
  bs.Write<float>(ent.omega);
  bs.Write<float>(ent.thr);
  bs.Write<float>(ent.steer);
  bs.Write<std::uint16_t>(ent.eid);
  if (connection->SendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

void send_set_controlled_entity(socketwire::ReliableConnection* connection,
                                std::uint16_t eid) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_SERVER_TO_CLIENT_SET_CONTROLLED_ENTITY);
  bs.Write<std::uint16_t>(eid);
  if (connection->SendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

void send_entity_input(socketwire::ReliableConnection* connection,
                       std::uint16_t eid, float thr, float steer) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_CLIENT_TO_SERVER_INPUT);
  bs.Write<std::uint16_t>(eid);
  bs.Write<float>(thr);
  bs.Write<float>(steer);
  if (connection->SendUnsequenced(1, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

void send_snapshot(socketwire::ReliableConnection* connection,
                   std::uint16_t eid, float x, float y, float ori, float vx,
                   float vy, float omega, TimePoint timestamp,
                   std::uint32_t frameNumber) {
  const auto duration = timestamp.time_since_epoch();
  const auto timestampMs = static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());

  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_SERVER_TO_CLIENT_SNAPSHOT);
  bs.Write<std::uint16_t>(eid);
  bs.Write<float>(x);
  bs.Write<float>(y);
  bs.Write<float>(ori);
  bs.Write<float>(vx);
  bs.Write<float>(vy);
  bs.Write<float>(omega);
  bs.Write<std::uint64_t>(timestampMs);
  bs.Write<std::uint32_t>(frameNumber);
  if (connection->SendUnsequenced(1, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

void send_time_msec(socketwire::ReliableConnection* connection,
                    std::uint32_t timeMsec) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(E_SERVER_TO_CLIENT_TIME_MSEC);
  bs.Write<std::uint32_t>(timeMsec);
  if (connection->SendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.GetSizeBytes());
}

MessageType get_packet_type(const void* data, std::size_t size) {
  if (data == nullptr || size < 1) return E_CLIENT_TO_SERVER_JOIN;
  return static_cast<MessageType>(*static_cast<const std::uint8_t*>(data));
}

void deserialize_new_entity(const void* data, std::size_t size, Entity& ent) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint32_t>(ent.color);
  bs.Read<float>(ent.x);
  bs.Read<float>(ent.y);
  bs.Read<std::uint16_t>(ent.eid);
  bs.Read<float>(ent.vx);
  bs.Read<float>(ent.vy);
  bs.Read<float>(ent.ori);
  bs.Read<float>(ent.omega);
  bs.Read<float>(ent.thr);
  bs.Read<float>(ent.steer);
  bs.Read<std::uint16_t>(ent.eid);
}

void deserialize_set_controlled_entity(const void* data, std::size_t size,
                                       std::uint16_t& eid) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);
}

void deserialize_entity_input(const void* data, std::size_t size,
                              std::uint16_t& eid, float& thr, float& steer) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);
  bs.Read<float>(thr);
  bs.Read<float>(steer);
}

void deserialize_snapshot(const void* data, std::size_t size,
                          std::uint16_t& eid, float& x, float& y, float& ori,
                          float& vx, float& vy, float& omega,
                          TimePoint& timestamp, std::uint32_t& frameNumber) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);
  bs.Read<float>(x);
  bs.Read<float>(y);
  bs.Read<float>(ori);
  bs.Read<float>(vx);
  bs.Read<float>(vy);
  bs.Read<float>(omega);

  std::uint64_t timestampMs = 0;
  bs.Read<std::uint64_t>(timestampMs);
  bs.Read<std::uint32_t>(frameNumber);
  timestamp = TimePoint(std::chrono::milliseconds(timestampMs));
}

void deserialize_time_msec(const void* data, std::size_t size,
                           std::uint32_t& timeMsec) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint32_t>(timeMsec);
}
