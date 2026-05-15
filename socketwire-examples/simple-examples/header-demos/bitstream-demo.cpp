#include "bit_stream.hpp"
#include <iostream>


int main()
{
  socketwire::BitStream stream;

  std::cout << "Test 1: Integer Types: \n" << std::endl;
  std::int8_t val1 = -128;
  std::uint16_t val2 = 65535;
  std::int32_t val3 = -2147483648;
  std::uint64_t val4 = 18446744073709551615ULL;

  stream.Write<std::int8_t>(val1);
  stream.Write<std::uint16_t>(val2);
  stream.Write<std::int32_t>(val3);
  stream.Write<std::uint64_t>(val4);

  std::cout << "Written: " << (int)val1 << ", " << val2 << ", " << val3 << ", " << val4
            << std::endl;
  std::cout << "Size after integers: " << stream.GetSizeBytes() << " bytes" << std::endl;

  std::cout << "\n\nTest 2: Strings: \n" << std::endl;
  std::string shortStr = "Hi";
  std::string longStr = "This is a much longer string to test serialization";
  std::string emptyStr = "";

  stream.Write(shortStr);
  stream.Write(longStr);
  stream.Write(emptyStr);

  std::cout << "Written strings: '" << shortStr << "', '" << longStr << "', '" << emptyStr << "'"
            << std::endl;
  std::cout << "Size after strings: " << stream.GetSizeBytes() << " bytes" << std::endl;

  std::cout << "\n\nTest 3: Bits: \n" << std::endl;
  bool bits[] = {true, false, true, true, false};
  for (int i = 0; i < 5; i++)
  {
    stream.WriteBit(bits[i]);
    std::cout << "Written bit " << i << ": " << bits[i] << std::endl;
  }

  stream.WriteBits(170, 8); // 10101010 в двоичном
  stream.WriteBits(15, 4);  // 1111 в двоичном
  std::cout << "Written 8 bits: 170 (10101010), 4 bits: 15 (1111)" << std::endl;
  std::cout << "Size after bits: " << stream.GetSizeBytes() << " bytes" << std::endl;

  std::cout << "\n\nTest 4: Quantized Floats: \n" << std::endl;
  float floats[] = {0.0f, 2.5f, 5.0f, 7.75f, 10.0f};
  for (int i = 0; i < 5; i++)
  {
    stream.WriteQuantizedFloat(floats[i], 0.0f, 10.0f, 8);
    std::cout << "Written quantized float: " << floats[i] << std::endl;
  }
  std::cout << "Final size: " << stream.GetSizeBytes() << " bytes" << std::endl;

  std::cout << "\n\nReading Back: " << std::endl;
  stream.ResetRead();

  std::int8_t rVal1;
  stream.Read<std::int8_t>(rVal1);
  std::uint16_t rVal2;
  stream.Read<std::uint16_t>(rVal2);
  std::int32_t rVal3;
  stream.Read<std::int32_t>(rVal3);
  std::uint64_t rVal4;
  stream.Read<std::uint64_t>(rVal4);

  std::cout << "Read integers: " << (int)rVal1 << ", " << rVal2
            << ", " << rVal3 << ", " << rVal4 << std::endl;

  std::string rShort, rLong, rEmpty;
  stream.Read(rShort);
  stream.Read(rLong);
  stream.Read(rEmpty);

  std::cout << "Read strings: '" << rShort << "', '" << rLong << "', '" << rEmpty << "'"
            << std::endl;

  std::cout << "Read bits: ";
  for (int i = 0; i < 5; i++)
    std::cout << stream.ReadBit() << " ";
  std::cout << std::endl;

  std::int32_t rBits8 = stream.ReadBits(8);
  std::int32_t rBits4 = stream.ReadBits(4);
  std::cout << "Read 8 bits: " << rBits8 << ", 4 bits: " << rBits4 << std::endl;

  std::cout << "Read quantized floats: ";
  for (int i = 0; i < 5; i++)
  {
    float qf = stream.ReadQuantizedFloat(0.0f, 10.0f, 8);
    std::cout << qf << " ";
  }
  std::cout << std::endl;

  return 0;
}
