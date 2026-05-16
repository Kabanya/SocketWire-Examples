#include "protocol.h"

#include "benchmark_utils.hpp"
#include "bit_stream.hpp"
#include "reliable_connection.hpp"

void SendJoin(socketwire::ReliableConnection* connection) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEClientToServerJoin);
  if (connection->SendReliable(0, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

void SendNewEntity(socketwire::ReliableConnection* connection,
                   const Entity& ent) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEServerToClientNewEntity);
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
  if (connection->SendReliable(0, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

void SendSetControlledEntity(socketwire::ReliableConnection* connection,
                             std::uint16_t eid) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEServerToClientSetControlledEntity);
  bs.Write<std::uint16_t>(eid);
  if (connection->SendReliable(0, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

void SendEntityInput(socketwire::ReliableConnection* connection,
                     std::uint16_t eid, float thr, float steer) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEClientToServerInput);
  bs.Write<std::uint16_t>(eid);
  bs.Write<float>(thr);
  bs.Write<float>(steer);
  if (connection->SendUnsequenced(1, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

void SendSnapshot(socketwire::ReliableConnection* connection, std::uint16_t eid,
                  float x, float y, float ori, float vx, float vy, float omega,
                  TimePoint timestamp, std::uint32_t frame_number) {
  const auto duration = timestamp.time_since_epoch();
  const auto timestamp_ms = static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());

  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEServerToClientSnapshot);
  bs.Write<std::uint16_t>(eid);
  bs.Write<float>(x);
  bs.Write<float>(y);
  bs.Write<float>(ori);
  bs.Write<float>(vx);
  bs.Write<float>(vy);
  bs.Write<float>(omega);
  bs.Write<std::uint64_t>(timestamp_ms);
  bs.Write<std::uint32_t>(frame_number);
  if (connection->SendUnsequenced(1, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

void SendTimeMsec(socketwire::ReliableConnection* connection,
                  std::uint32_t time_msec) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEServerToClientTimeMsec);
  bs.Write<std::uint32_t>(time_msec);
  if (connection->SendReliable(0, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

MessageType GetPacketType(const void* data, std::size_t size) {
  if (data == nullptr || size < 1) return kEClientToServerJoin;
  return static_cast<MessageType>(*static_cast<const std::uint8_t*>(data));
}

void DeserializeNewEntity(const void* data, std::size_t size, Entity& ent) {
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

void DeserializeSetControlledEntity(const void* data, std::size_t size,
                                    std::uint16_t& eid) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);
}

void DeserializeEntityInput(const void* data, std::size_t size,
                            std::uint16_t& eid, float& thr, float& steer) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);
  bs.Read<float>(thr);
  bs.Read<float>(steer);
}

void DeserializeSnapshot(const void* data, std::size_t size, std::uint16_t& eid,
                         float& x, float& y, float& ori, float& vx, float& vy,
                         float& omega, TimePoint& timestamp,
                         std::uint32_t& frame_number) {
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

  std::uint64_t timestamp_ms = 0;
  bs.Read<std::uint64_t>(timestamp_ms);
  bs.Read<std::uint32_t>(frame_number);
  timestamp = TimePoint(std::chrono::milliseconds(timestamp_ms));
}

void DeserializeTimeMsec(const void* data, std::size_t size,
                         std::uint32_t& time_msec) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint32_t>(time_msec);
}
