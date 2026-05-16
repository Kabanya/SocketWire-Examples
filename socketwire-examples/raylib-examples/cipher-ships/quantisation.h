#pragma once
#include <cstdint>

#include "mathUtils.h"

template <typename T>
T PackFloat(float v, float lo, float hi, int num_bits) {
  T range = (1 << num_bits) - 1;  // std::numeric_limits<T>::max();
  return range * ((Clamp(v, lo, hi) - lo) / (hi - lo));
}

template <typename T>
float UnpackFloat(T c, float lo, float hi, int num_bits) {
  T range = (1 << num_bits) - 1;  // std::numeric_limits<T>::max();
  return float(c) / range * (hi - lo) + lo;
}

template <typename T, int num_bits>
struct PackedFloat {
  T packedVal;

  PackedFloat(float v, float lo, float hi) { Pack(v, lo, hi); }
  explicit PackedFloat(T compressed_val) : packedVal(compressed_val) {}

  void Pack(float v, float lo, float hi) {
    packedVal = pack_float<T>(v, lo, hi, num_bits);
  }
  float Unpack(float lo, float hi) {
    return unpack_float<T>(packedVal, lo, hi, num_bits);
  }
};

using float4bitsQuantized = PackedFloat<uint8_t, 4>;
