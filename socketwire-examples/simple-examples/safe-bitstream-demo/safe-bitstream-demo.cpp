#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include "bit_stream.hpp"

using socketwire::BitStream;
using socketwire::BitStreamError;

static void print_error(const char* field, BitStreamError error) {
  std::printf("%s failed with %s\n", field, socketwire::ToString(error));
}

static bool decode_player_packet(std::span<const std::uint8_t> bytes) {
  BitStream stream(bytes.data(), bytes.size());

  const auto tick = stream.TryRead<std::uint32_t>();
  if (!tick) {
    print_error("tick", tick.error());
    return false;
  }

  const auto name = stream.TryReadString();
  if (!name) {
    print_error("name", name.error());
    return false;
  }

  const auto flags = stream.TryReadBoolArray();
  if (!flags) {
    print_error("flags", flags.error());
    return false;
  }

  const auto buttons = stream.TryReadBits(4);
  if (!buttons) {
    print_error("buttons", buttons.error());
    return false;
  }

  std::printf("decoded: tick=%u name=%s flags=%zu buttons=0x%x\n", *tick,
              name->c_str(), flags->size(), *buttons);
  return true;
}

static std::vector<std::uint8_t> make_valid_packet() {
  BitStream stream;
  stream.Write<std::uint32_t>(42);
  stream.Write(std::string("demo-player"));
  stream.WriteBoolArray({true, false, true, true, false});
  stream.WriteBits(0b1011, 4);

  const auto* first = stream.GetData();
  return {first, first + stream.GetSizeBytes()};
}

static std::vector<std::uint8_t> make_bad_string_length_packet() {
  BitStream stream;
  stream.Write<std::uint32_t>(70000);
  const auto* first = stream.GetData();
  return {first, first + stream.GetSizeBytes()};
}

static std::vector<std::uint8_t> make_bad_bool_array_packet() {
  BitStream stream;
  stream.Write<std::uint32_t>(8);
  stream.Write(std::string("bad-flags"));
  stream.Write<std::uint32_t>(32);
  stream.WriteBit(true);

  const auto* first = stream.GetData();
  return {first, first + stream.GetSizeBytes()};
}

int main() {
  const auto valid = make_valid_packet();
  std::printf("valid packet:\n");
  decode_player_packet(valid);

  auto truncated = valid;
  truncated.resize(truncated.size() - 3);
  std::printf("\ntruncated packet:\n");
  decode_player_packet(truncated);

  std::printf("\ninvalid string length:\n");
  const auto badString = make_bad_string_length_packet();
  BitStream badStringStream(badString.data(), badString.size());
  const auto name = badStringStream.TryReadString();
  if (!name) print_error("string", name.error());

  std::printf("\ninvalid bool array payload:\n");
  decode_player_packet(make_bad_bool_array_packet());

  return 0;
}
