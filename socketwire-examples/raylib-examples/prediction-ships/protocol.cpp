#include "protocol.h"

#include "benchmark_utils.hpp"
#include "bit_stream.hpp"
#include "reliable_connection.hpp"

namespace {

template <typename T>
bool Read(socketwire::BitStream& stream, T& value) {
  const auto read = stream.TryRead<T>();
  if (!read) return false;
  value = *read;
  return true;
}

}  // namespace

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
  if (connection->SendUnreliable(1, bs)) {
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
  if (connection->SendUnreliable(1, bs)) {
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

bool DeserializeNewEntity(const void* data, std::size_t size, Entity& ent) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  return Read(bs, type) && Read(bs, ent.color) && Read(bs, ent.x) &&
         Read(bs, ent.y) && Read(bs, ent.eid) && Read(bs, ent.vx) &&
         Read(bs, ent.vy) && Read(bs, ent.ori) && Read(bs, ent.omega) &&
         Read(bs, ent.thr) && Read(bs, ent.steer) && Read(bs, ent.eid);
}

bool DeserializeSetControlledEntity(const void* data, std::size_t size,
                                    std::uint16_t& eid) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  return Read(bs, type) && Read(bs, eid);
}

bool DeserializeEntityInput(const void* data, std::size_t size,
                            std::uint16_t& eid, float& thr, float& steer) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  return Read(bs, type) && Read(bs, eid) && Read(bs, thr) && Read(bs, steer);
}

bool DeserializeSnapshot(const void* data, std::size_t size, std::uint16_t& eid,
                         float& x, float& y, float& ori, float& vx, float& vy,
                         float& omega, TimePoint& timestamp,
                         std::uint32_t& frame_number) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  std::uint64_t timestamp_ms = 0;
  if (!Read(bs, type) || !Read(bs, eid) || !Read(bs, x) || !Read(bs, y) ||
      !Read(bs, ori) || !Read(bs, vx) || !Read(bs, vy) ||
      !Read(bs, omega) || !Read(bs, timestamp_ms) ||
      !Read(bs, frame_number)) {
    return false;
  }
  timestamp = TimePoint(std::chrono::milliseconds(timestamp_ms));
  return true;
}

bool DeserializeTimeMsec(const void* data, std::size_t size,
                         std::uint32_t& time_msec) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  return Read(bs, type) && Read(bs, time_msec);
}
