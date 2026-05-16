#pragma once

#include <cstdint>
#include <string>

#include "bit_stream.hpp"

namespace channels_demo {

constexpr std::uint16_t kKPort = 53474;

enum class MessageType : std::uint8_t {
  kCommand = 1,
  kCommandAck = 2,
  kMovement = 3,
  kSnapshot = 4,
};

inline socketwire::BitStream MakeCommand(std::uint32_t command_id,
                                         const std::string& text) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::kCommand));
  stream.Write<std::uint32_t>(command_id);
  stream.Write(text);
  return stream;
}

inline socketwire::BitStream MakeCommandAck(std::uint32_t command_id) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(
    static_cast<std::uint8_t>(MessageType::kCommandAck));
  stream.Write<std::uint32_t>(command_id);
  return stream;
}

inline socketwire::BitStream MakeMovement(std::uint32_t tick, float x,
                                          float y) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::kMovement));
  stream.Write<std::uint32_t>(tick);
  stream.Write<float>(x);
  stream.Write<float>(y);
  return stream;
}

inline socketwire::BitStream MakeSnapshot(std::uint32_t tick, float x,
                                          float y) {
  socketwire::BitStream stream;
  stream.Write<std::uint8_t>(static_cast<std::uint8_t>(MessageType::kSnapshot));
  stream.Write<std::uint32_t>(tick);
  stream.Write<float>(x);
  stream.Write<float>(y);
  return stream;
}

}  // namespace channels_demo
