#include <cstdint>
#include <cstdio>
#include <print>
#include <span>
#include <string>
#include <vector>

#include "bit_stream.hpp"

using socketwire::BitStream;
using socketwire::BitStreamError;

static void PrintError(const char* field, BitStreamError error) {
  std::println("{} failed with {}", field, socketwire::ToString(error));
}

static bool DecodePlayerPacket(std::span<const std::uint8_t> bytes) {
  BitStream stream(bytes.data(), bytes.size());

  const auto tick = stream.TryRead<std::uint32_t>();
  if (!tick) {
    PrintError("tick", tick.error());
    return false;
  }

  const auto name = stream.TryReadString();
  if (!name) {
    PrintError("name", name.error());
    return false;
  }

  const auto flags = stream.TryReadBoolArray();
  if (!flags) {
    PrintError("flags", flags.error());
    return false;
  }

  const auto buttons = stream.TryReadBits(4);
  if (!buttons) {
    PrintError("buttons", buttons.error());
    return false;
  }

  std::println("decoded: tick={} name={} flags={} buttons=0x{:x}", *tick, *name,
               flags->size(), *buttons);
  return true;
}

static std::vector<std::uint8_t> MakeValidPacket() {
  BitStream stream;
  stream.Write<std::uint32_t>(42);
  stream.Write(std::string("demo-player"));
  stream.WriteBoolArray({true, false, true, true, false});
  stream.WriteBits(0b1011, 4);

  const auto* first = stream.GetData();
  return {first, first + stream.GetSizeBytes()};
}

static std::vector<std::uint8_t> MakeBadStringLengthPacket() {
  BitStream stream;
  stream.Write<std::uint32_t>(70000);
  const auto* first = stream.GetData();
  return {first, first + stream.GetSizeBytes()};
}

static std::vector<std::uint8_t> MakeBadBoolArrayPacket() {
  BitStream stream;
  stream.Write<std::uint32_t>(8);
  stream.Write(std::string("bad-flags"));
  stream.Write<std::uint32_t>(32);
  stream.WriteBit(true);

  const auto* first = stream.GetData();
  return {first, first + stream.GetSizeBytes()};
}

int main() {
  const auto valid = MakeValidPacket();
  std::println("valid packet:");
  DecodePlayerPacket(valid);

  auto truncated = valid;
  truncated.resize(truncated.size() - 3);
  std::println("\ntruncated packet:");
  DecodePlayerPacket(truncated);

  std::println("\ninvalid string length:");
  const auto bad_string = MakeBadStringLengthPacket();
  BitStream bad_string_stream(bad_string.data(), bad_string.size());
  const auto name = bad_string_stream.TryReadString();
  if (!name) PrintError("string", name.error());

  std::println("\ninvalid bool array payload:");
  DecodePlayerPacket(MakeBadBoolArrayPacket());

  return 0;
}
