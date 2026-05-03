#pragma once

#include "bit_stream.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace large_message_demo
{

constexpr std::uint16_t kPort = 53475;
constexpr std::size_t kPayloadSize = 4096;

enum class MessageType : std::uint8_t
{
  Blob = 1,
  BlobAck = 2,
};

inline std::uint32_t checksum(std::vector<std::uint8_t> const& bytes)
{
  std::uint32_t hash = 2166136261u;
  for (const auto byte : bytes)
  {
    hash ^= byte;
    hash *= 16777619u;
  }
  return hash;
}

inline std::vector<std::uint8_t> make_payload()
{
  std::vector<std::uint8_t> payload(kPayloadSize);
  for (std::size_t i = 0; i < payload.size(); ++i)
    payload[i] = static_cast<std::uint8_t>((i * 31u + i / 7u) & 0xffu);
  return payload;
}

inline socketwire::BitStream make_blob(std::vector<std::uint8_t> const& payload)
{
  socketwire::BitStream stream;
  stream.write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::Blob));
  stream.write<std::uint32_t>(static_cast<std::uint32_t>(payload.size()));
  stream.write<std::uint32_t>(checksum(payload));
  stream.writeBytes(payload.data(), payload.size());
  return stream;
}

inline socketwire::BitStream make_ack(std::uint32_t size, std::uint32_t expected, std::uint32_t actual)
{
  socketwire::BitStream stream;
  stream.write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::BlobAck));
  stream.write<std::uint32_t>(size);
  stream.write<std::uint32_t>(expected);
  stream.write<std::uint32_t>(actual);
  return stream;
}

} // namespace large_message_demo
