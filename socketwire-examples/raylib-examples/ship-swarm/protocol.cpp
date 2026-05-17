#include "protocol.h"

#include <cstdint>

#include "benchmark_utils.hpp"
#include "bit_stream.hpp"
#include "quantisation.h"
#include "reliable_connection.hpp"

namespace {

void WriteEntity(socketwire::BitStream& bs, const Entity& ent) {
  bs.Write<std::uint32_t>(ent.color);
  bs.Write<bool>(ent.serverControlled);
  bs.Write<float>(ent.x);
  bs.Write<float>(ent.y);
  bs.Write<float>(ent.vx);
  bs.Write<float>(ent.vy);
  bs.Write<float>(ent.ori);
  bs.Write<float>(ent.omega);
  bs.Write<float>(ent.thr);
  bs.Write<float>(ent.steer);
  bs.Write<std::uint16_t>(ent.eid);
}

void ReadEntity(socketwire::BitStream& bs, Entity& ent) {
  bs.Read<std::uint32_t>(ent.color);
  bs.Read<bool>(ent.serverControlled);
  bs.Read<float>(ent.x);
  bs.Read<float>(ent.y);
  bs.Read<float>(ent.vx);
  bs.Read<float>(ent.vy);
  bs.Read<float>(ent.ori);
  bs.Read<float>(ent.omega);
  bs.Read<float>(ent.thr);
  bs.Read<float>(ent.steer);
  bs.Read<std::uint16_t>(ent.eid);
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
  WriteEntity(bs, ent);
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

  const Float4bitsQuantized thr_packed(thr, -1.f, 1.f);
  const Float4bitsQuantized steer_packed(steer, -1.f, 1.f);
  const auto thr_steer_packed = static_cast<std::uint8_t>(
    (thr_packed.packedVal << 4) | steer_packed.packedVal);
  bs.Write<std::uint8_t>(thr_steer_packed);

  if (connection->SendUnreliable(1, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

using PositionXQuantized = PackedFloat<std::uint16_t, 11>;
using PositionYQuantized = PackedFloat<std::uint16_t, 10>;

void SendSnapshot(socketwire::ReliableConnection* connection, std::uint16_t eid,
                  float x, float y, float ori) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEServerToClientSnapshot);
  bs.Write<std::uint16_t>(eid);

  const PositionXQuantized x_packed(x, -kWorldSize, kWorldSize);
  const PositionYQuantized y_packed(y, -kWorldSize, kWorldSize);
  const auto ori_packed = PackFloat<std::uint8_t>(ori, -kPi, kPi, 8);
  bs.Write<std::uint16_t>(x_packed.packedVal);
  bs.Write<std::uint16_t>(y_packed.packedVal);
  bs.Write<std::uint8_t>(ori_packed);

  if (connection->SendUnreliable(1, bs)) {
    socketwire_examples::benchmark::RecordPayloadTx(bs.GetSizeBytes());
  }
}

void SendTimeMsec(socketwire::ReliableConnection* connection,
                  std::uint32_t time_msec) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEServerToClientTimeMsec);
  bs.Write<std::uint32_t>(time_msec);
  if (connection->SendUnreliable(0, bs)) {
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
  ReadEntity(bs, ent);
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

  std::uint8_t thr_steer_packed = 0;
  bs.Read<std::uint8_t>(thr_steer_packed);

  static const auto kNeutralPackedValue =
    PackFloat<std::uint8_t>(0.f, -1.f, 1.f, 4);
  Float4bitsQuantized thr_packed(thr_steer_packed >> 4);
  Float4bitsQuantized steer_packed(thr_steer_packed & 0x0f);
  thr = thr_packed.packedVal == kNeutralPackedValue
          ? 0.f
          : thr_packed.Unpack(-1.f, 1.f);
  steer = steer_packed.packedVal == kNeutralPackedValue
            ? 0.f
            : steer_packed.Unpack(-1.f, 1.f);
}

void DeserializeSnapshot(const void* data, std::size_t size, std::uint16_t& eid,
                         float& x, float& y, float& ori) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint16_t>(eid);

  std::uint16_t x_packed = 0;
  std::uint16_t y_packed = 0;
  std::uint8_t ori_packed = 0;
  bs.Read<std::uint16_t>(x_packed);
  bs.Read<std::uint16_t>(y_packed);
  bs.Read<std::uint8_t>(ori_packed);

  PositionXQuantized x_packed_val(x_packed);
  PositionYQuantized y_packed_val(y_packed);
  x = x_packed_val.Unpack(-kWorldSize, kWorldSize);
  y = y_packed_val.Unpack(-kWorldSize, kWorldSize);
  ori = UnpackFloat<std::uint8_t>(ori_packed, -kPi, kPi, 8);
}

void DeserializeTimeMsec(const void* data, std::size_t size,
                         std::uint32_t& time_msec) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint32_t>(time_msec);
}
