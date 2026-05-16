#include "entity.h"

#include "mathUtils.h"

constexpr float kWorldSize = 30.f;

float TileVal(float val, float border) {
  if (val < -border) {
    return val + 2.f * border;
  } else if (val > border) {
    return val - 2.f * border;
  }
  return val;
}

void SimulateEntity(Entity& e, float dt) {
  bool const is_braking = Sign(e.thr) < 0.f;
  // float accel = isBraking ? 6.f : 1.5f;
  float const accel = is_braking ? 12.f : 3.5f;
  float const va = Clamp(e.thr, -0.3, 3.f) * accel;
  e.vx += cosf(e.ori) * va * dt;
  e.vy += sinf(e.ori) * va * dt;
  e.omega += e.steer * dt * 0.3f;
  e.ori += e.omega * dt;
  e.x += e.vx * dt;
  e.y += e.vy * dt;

  e.x = TileVal(e.x, kWorldSize);
  e.y = TileVal(e.y, kWorldSize);
}
