#pragma once
#include <cstdint>

constexpr uint16_t INVALID_ENTITY = -1;
struct Entity
{
  uint32_t color = 0xff00ffff;
  float x = 0.f;
  float y = 0.f;
  uint16_t eid = INVALID_ENTITY;
  bool serverControlled = false;
  float targetX = 0.f;
  float targetY = 0.f;
  float size = 10.0f;
  int score = 0;
};

