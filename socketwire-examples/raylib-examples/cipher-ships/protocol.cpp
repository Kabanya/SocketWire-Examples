#include "protocol.h"

#include "benchmark_utils.hpp"
#include "bit_stream.hpp"
#include "quantisation.h"
#include "reliable_connection.hpp"

#include <cstring>
#include <random>

static std::uint32_t xorCipherKey = 0;

static std::mt19937& random_generator()
{
  static std::random_device random_device;
  static std::mt19937 generator(random_device());
  return generator;
}

static std::vector<std::uint8_t> copy_stream(const socketwire::BitStream& bs)
{
  const auto* data = bs.getData();
  return std::vector<std::uint8_t>(data, data + bs.getSizeBytes());
}

static void xor_packet_data(std::vector<std::uint8_t>& packet, std::uint32_t key)
{
  auto* keyPtr = reinterpret_cast<std::uint8_t*>(&key);
  for (std::size_t i = 1; i < packet.size(); ++i)
    packet[i] ^= keyPtr[(i - 1) % sizeof(std::uint32_t)];
}

static void fuzz_packet_data(std::vector<std::uint8_t>& packet)
{
  if (!packet.empty())
  {
    std::uniform_int_distribution<std::size_t> index_distribution(0, packet.size() - 1);
    std::uniform_int_distribution<int> byte_distribution(0, 255);
    packet[index_distribution(random_generator())] =
      static_cast<std::uint8_t>(byte_distribution(random_generator()));
  }
}

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
  bs.writeBytes(&ent, sizeof(Entity));
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

void send_cipher_key(socketwire::ReliableConnection* connection, std::uint32_t key)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_SERVER_TO_CLIENT_KEY);
  bs.write<std::uint32_t>(key);
  if (connection->sendReliable(0, bs))
    socketwire_examples::benchmark::recordPayloadTx(bs.getSizeBytes());
}

void send_entity_input(socketwire::ReliableConnection* connection, std::uint16_t eid, float thr, float steer)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_CLIENT_TO_SERVER_INPUT);
  bs.write<std::uint16_t>(eid);
  bs.write<float>(thr);
  bs.write<float>(steer);

  std::vector<std::uint8_t> packet = copy_stream(bs);
  fuzz_packet_data(packet);
  xor_packet_data(packet, xorCipherKey);
  if (connection->sendUnsequenced(1, packet.data(), packet.size()))
    socketwire_examples::benchmark::recordPayloadTx(packet.size());
}

void send_snapshot(socketwire::ReliableConnection* connection, std::uint16_t eid, float x, float y, float ori)
{
  socketwire::BitStream bs;
  bs.write<std::uint8_t>(E_SERVER_TO_CLIENT_SNAPSHOT);
  bs.write<std::uint16_t>(eid);

  const std::uint16_t xPacked = pack_float<std::uint16_t>(x, -16.f, 16.f, 11);
  const std::uint16_t yPacked = pack_float<std::uint16_t>(y, -8.f, 8.f, 10);
  const std::uint8_t oriPacked = pack_float<std::uint8_t>(ori, -PI, PI, 8);
  bs.write<std::uint16_t>(xPacked);
  bs.write<std::uint16_t>(yPacked);
  bs.write<std::uint8_t>(oriPacked);

  if (connection->sendUnsequenced(1, bs))
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
  bs.readBytes(&ent, sizeof(Entity));
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

void deserialize_snapshot(const void* data, std::size_t size, std::uint16_t& eid, float& x, float& y, float& ori)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.read<std::uint8_t>(type);
  bs.read<std::uint16_t>(eid);

  std::uint16_t xPacked = 0;
  std::uint16_t yPacked = 0;
  std::uint8_t oriPacked = 0;
  bs.read<std::uint16_t>(xPacked);
  bs.read<std::uint16_t>(yPacked);
  bs.read<std::uint8_t>(oriPacked);

  x = unpack_float<std::uint16_t>(xPacked, -16.f, 16.f, 11);
  y = unpack_float<std::uint16_t>(yPacked, -8.f, 8.f, 10);
  ori = unpack_float<std::uint8_t>(oriPacked, -PI, PI, 8);
}

void deserialize_and_set_key(const void* data, std::size_t size)
{
  socketwire::BitStream bs(static_cast<const std::uint8_t*>(data), size);
  std::uint8_t type = 0;
  bs.read<std::uint8_t>(type);
  bs.read<std::uint32_t>(xorCipherKey);
}

std::vector<std::uint8_t> decipher_data(const void* data, std::size_t size, std::uint32_t key)
{
  const auto* begin = static_cast<const std::uint8_t*>(data);
  std::vector<std::uint8_t> packet(begin, begin + size);
  xor_packet_data(packet, key);
  return packet;
}
