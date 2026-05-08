#pragma once

#include "bit_stream.hpp"

#include <cstdint>

namespace stats_window_demo
{

constexpr std::uint16_t K_PORT = 53476;
constexpr std::uint32_t K_PACKET_COUNT = 24;

enum class MessageType : std::uint8_t
{
  Sample = 1,
  SampleAck = 2,
};

inline socketwire::BitStream make_sample(std::uint32_t id)
{
  socketwire::BitStream stream;
  stream.write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::Sample));
  stream.write<std::uint32_t>(id);
  return stream;
}

inline socketwire::BitStream make_sample_ack(std::uint32_t id)
{
  socketwire::BitStream stream;
  stream.write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::SampleAck));
  stream.write<std::uint32_t>(id);
  return stream;
}

} // namespace stats_window_demo
