#pragma once
#include <math.h>

inline float MoveTo(float from, float to, float dt, float vel) {
  float const d = vel * dt;
  if (fabsf(from - to) < d) return to;

  if (to < from) {
    return from - d;
  } else {
    return from + d;
  }
}

inline float Clamp(float in, float min, float max) {
  return in < min ? min : in > max ? max : in;
}

inline float Sign(float in) { return in > 0.f ? 1.f : in < 0.f ? -1.f : 0.f; }

constexpr float kPi = 3.141592654f;
