#pragma once

#include <cstdint>

#include "bit_stream.hpp"

namespace stats_window_demo {

constexpr std::uint16_t kKPort = 53476;
constexpr std::uint32_t kKPacketCount = 24;

enum class MessageType : std::uint8_t {
  kSample = 1,
  kSampleAck = 2,
};

inline socketwire::BitStream MakeSample(std::uint32_t id) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::kSample));
  stream.Write<std::uint32_t>(id);
  return stream;
}

inline socketwire::BitStream MakeSampleAck(std::uint32_t id) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(
    static_cast<std::uint8_t>(MessageType::kSampleAck));
  stream.Write<std::uint32_t>(id);
  return stream;
}

}  // namespace stats_window_demo
