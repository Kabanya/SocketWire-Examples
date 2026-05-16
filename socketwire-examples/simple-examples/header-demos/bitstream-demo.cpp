#include <iostream>

#include "bit_stream.hpp"

int main() {
  socketwire::BitStream stream;

  std::cout << "Test 1: Integer Types: \n" << '\n';
  std::int8_t val1 = -128;
  std::uint16_t val2 = 65535;
  std::int32_t val3 = -2147483648;
  std::uint64_t val4 = 18446744073709551615ULL;

  stream.Write<std::int8_t>(val1);
  stream.Write<std::uint16_t>(val2);
  stream.Write<std::int32_t>(val3);
  stream.Write<std::uint64_t>(val4);

  std::cout << "Written: " << static_cast<int>(val1) << ", " << val2 << ", "
            << val3 << ", " << val4 << '\n';
  std::cout << "Size after integers: " << stream.GetSizeBytes() << " bytes"
            << '\n';

  std::cout << "\n\nTest 2: Strings: \n" << '\n';
  std::string const short_str = "Hi";
  std::string const long_str =
    "This is a much longer string to test serialization";
  std::string const empty_str = "";

  stream.Write(short_str);
  stream.Write(long_str);
  stream.Write(empty_str);

  std::cout << "Written strings: '" << short_str << "', '" << long_str << "', '"
            << empty_str << "'" << '\n';
  std::cout << "Size after strings: " << stream.GetSizeBytes() << " bytes"
            << '\n';

  std::cout << "\n\nTest 3: Bits: \n" << '\n';
  bool const bits[] = {true, false, true, true, false};
  for (int i = 0; i < 5; i++) {
    stream.WriteBit(bits[i]);
    std::cout << "Written bit " << i << ": " << bits[i] << '\n';
  }

  stream.WriteBits(170, 8);  // 10101010 в двоичном
  stream.WriteBits(15, 4);   // 1111 в двоичном
  std::cout << "Written 8 bits: 170 (10101010), 4 bits: 15 (1111)" << '\n';
  std::cout << "Size after bits: " << stream.GetSizeBytes() << " bytes" << '\n';

  std::cout << "\n\nTest 4: Quantized Floats: \n" << '\n';
  float const floats[] = {0.0f, 2.5f, 5.0f, 7.75f, 10.0f};
  for (float i : floats) {
    stream.WriteQuantizedFloat(i, 0.0f, 10.0f, 8);
    std::cout << "Written quantized float: " << i << '\n';
  }
  std::cout << "Final size: " << stream.GetSizeBytes() << " bytes" << '\n';

  std::cout << "\n\nReading Back: " << '\n';
  stream.ResetRead();

  std::int8_t r_val1 = 0;
  stream.Read<std::int8_t>(r_val1);
  std::uint16_t r_val2 = 0;
  stream.Read<std::uint16_t>(r_val2);
  std::int32_t r_val3 = 0;
  stream.Read<std::int32_t>(r_val3);
  std::uint64_t r_val4 = 0;
  stream.Read<std::uint64_t>(r_val4);

  std::cout << "Read integers: " << static_cast<int>(r_val1) << ", " << r_val2
            << ", " << r_val3 << ", " << r_val4 << '\n';

  std::string r_short, r_long, r_empty;
  stream.Read(r_short);
  stream.Read(r_long);
  stream.Read(r_empty);

  std::cout << "Read strings: '" << r_short << "', '" << r_long << "', '"
            << r_empty << "'" << '\n';

  std::cout << "Read bits: ";
  for (int i = 0; i < 5; i++) std::cout << stream.ReadBit() << " ";
  std::cout << '\n';

  std::int32_t r_bits8 = stream.ReadBits(8);
  std::int32_t r_bits4 = stream.ReadBits(4);
  std::cout << "Read 8 bits: " << r_bits8 << ", 4 bits: " << r_bits4 << '\n';

  std::cout << "Read quantized floats: ";
  for (int i = 0; i < 5; i++) {
    float const qf = stream.ReadQuantizedFloat(0.0f, 10.0f, 8);
    std::cout << qf << " ";
  }
  std::cout << '\n';

  return 0;
}
