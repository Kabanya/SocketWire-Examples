#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "bit_stream.hpp"

namespace large_message_demo {

constexpr std::uint16_t kKPort = 53475;
constexpr std::size_t kKPayloadSize = 4096;

enum class MessageType : std::uint8_t {
  kBlob = 1,
  kBlobAck = 2,
};

inline std::uint32_t Checksum(std::vector<std::uint8_t> const& bytes) {
  std::uint32_t hash = 2166136261u;
  for (const auto byte : bytes) {
    hash ^= byte;
    hash *= 16777619u;
  }
  return hash;
}

inline std::vector<std::uint8_t> MakePayload() {
  std::vector<std::uint8_t> payload(kKPayloadSize);
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>((i * 31u + i / 7u) & 0xffu);
  }
  return payload;
}

inline socketwire::BitStream MakeBlob(
  std::vector<std::uint8_t> const& payload) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::kBlob));
  stream.Write<std::uint32_t>(static_cast<std::uint32_t>(payload.size()));
  stream.Write<std::uint32_t>(Checksum(payload));
  stream.WriteBytes(payload.data(), payload.size());
  return stream;
}

inline socketwire::BitStream MakeAck(std::uint32_t size, std::uint32_t expected,
                                     std::uint32_t actual) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::kBlobAck));
  stream.Write<std::uint32_t>(size);
  stream.Write<std::uint32_t>(expected);
  stream.Write<std::uint32_t>(actual);
  return stream;
}

}  // namespace large_message_demo
