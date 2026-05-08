#pragma once

#include "bit_stream.hpp"

#include <cstdint>
#include <string>

namespace channels_demo
{

constexpr std::uint16_t K_PORT = 53474;

enum class MessageType : std::uint8_t
{
  Command = 1,
  CommandAck = 2,
  Movement = 3,
  Snapshot = 4,
};

inline socketwire::BitStream make_command(std::uint32_t commandId, const std::string& text)
{
  socketwire::BitStream stream;
  stream.write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::Command));
  stream.write<std::uint32_t>(commandId);
  stream.write(text);
  return stream;
}

inline socketwire::BitStream make_command_ack(std::uint32_t commandId)
{
  socketwire::BitStream stream;
  stream.write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::CommandAck));
  stream.write<std::uint32_t>(commandId);
  return stream;
}

inline socketwire::BitStream make_movement(std::uint32_t tick, float x, float y)
{
  socketwire::BitStream stream;
  stream.write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::Movement));
  stream.write<std::uint32_t>(tick);
  stream.write<float>(x);
  stream.write<float>(y);
  return stream;
}

inline socketwire::BitStream make_snapshot(std::uint32_t tick, float x, float y)
{
  socketwire::BitStream stream;
  stream.write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::Snapshot));
  stream.write<std::uint32_t>(tick);
  stream.write<float>(x);
  stream.write<float>(y);
  return stream;
}

} // namespace channels_demo
