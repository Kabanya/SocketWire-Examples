#include "protocol.h"

#include <cstring>
#include <random>

#include "benchmark_utils.hpp"
#include "bit_stream.hpp"
#include "quantisation.h"
#include "reliable_connection.hpp"

static std::uint32_t xor_cipher_key = 0;

static std::mt19937& RandomGenerator() {
  static std::random_device random_device;
  static std::mt19937 generator(random_device());
  return generator;
}

static std::vector<std::uint8_t> CopyStream(const socketwire::BitStream& bs) {
  const auto* data = bs.GetData();
  return {data, data + bs.GetSizeBytes()};
}

static void XorPacketData(std::vector<std::uint8_t>& packet,
                          std::uint32_t key) {
  auto* key_ptr = reinterpret_cast<std::uint8_t*>(&key);
  for (std::size_t i = 1; i < packet.size(); ++i) {
    packet[i] ^= key_ptr[(i - 1) % sizeof(std::uint32_t)];
  }
}

static void FuzzPacketData(std::vector<std::uint8_t>& packet) {
  if (!packet.empty()) {
    std::uniform_int_distribution<std::size_t> index_distribution(
      0, packet.size() - 1);
    std::uniform_int_distribution<int> byte_distribution(0, 255);
    packet[index_distribution(RandomGenerator())] =
      static_cast<std::uint8_t>(byte_distribution(RandomGenerator()));
  }
}

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
  bs.WriteBytes(&ent, sizeof(Entity));
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

void SendCipherKey(socketwire::ReliableConnection* connection,
                   std::uint32_t key) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEServerToClientKey);
  bs.Write<std::uint32_t>(key);
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

  std::vector<std::uint8_t> packet = CopyStream(bs);
  FuzzPacketData(packet);
  XorPacketData(packet, xor_cipher_key);
  if (connection->SendUnsequenced(1, packet.data(), packet.size())) {
    socketwire_examples::benchmark::RecordPayloadTx(packet.size());
  }
}

void SendSnapshot(socketwire::ReliableConnection* connection, std::uint16_t eid,
                  float x, float y, float ori) {
  socketwire::BitStream bs;
  bs.Write<std::uint8_t>(kEServerToClientSnapshot);
  bs.Write<std::uint16_t>(eid);

  const auto x_packed = PackFloat<std::uint16_t>(x, -16.f, 16.f, 11);
  const auto y_packed = PackFloat<std::uint16_t>(y, -8.f, 8.f, 10);
  const auto ori_packed = PackFloat<std::uint8_t>(ori, -kPi, kPi, 8);
  bs.Write<std::uint16_t>(x_packed);
  bs.Write<std::uint16_t>(y_packed);
  bs.Write<std::uint8_t>(ori_packed);

  if (connection->SendUnsequenced(1, bs)) {
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
  bs.ReadBytes(&ent, sizeof(Entity));
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

  x = UnpackFloat<std::uint16_t>(x_packed, -16.f, 16.f, 11);
  y = UnpackFloat<std::uint16_t>(y_packed, -8.f, 8.f, 10);
  ori = UnpackFloat<std::uint8_t>(ori_packed, -kPi, kPi, 8);
}

void DeserializeAndSetKey(const void* data, std::size_t size) {
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.Read<std::uint8_t>(type);
  bs.Read<std::uint32_t>(xor_cipher_key);
}

std::vector<std::uint8_t> DecipherData(const void* data, std::size_t size,
                                       std::uint32_t key) {
  const auto* begin = static_cast<const std::uint8_t*>(data);
  std::vector<std::uint8_t> packet(begin, begin + size);
  XorPacketData(packet, key);
  return packet;
}
