#pragma once
#include <cstdint>

constexpr uint16_t invalid_entity = -1;
struct Entity
{
  uint32_t color = 0xff00ffff;
  float x = 0.f;
  float y = 0.f;
  float speed = 0.f;
  float ori = 0.f;
  float thr = 0.f;
  float steer = 0.f;
  uint16_t eid = invalid_entity;
};

void simulate_entity(Entity &e, float dt);

