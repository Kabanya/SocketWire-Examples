#include "entity.h"
#include "mathUtils.h"

float tile_val(float val, float border)
{
  if (val < -border)
    return val + 2.f * border;
  else if (val > border)
    return val - 2.f * border;
  return val;
}

void simulate_entity(Entity &e, float dt)
{
  bool isBraking = sign(e.thr) < 0.f;
  float accel = isBraking ? 6.f : 1.5f;
  float va = clamp(e.thr, -0.3, 1.f) * accel;
  e.vx += cosf(e.ori) * va * dt;
  e.vy += sinf(e.ori) * va * dt;
  e.omega += e.steer * dt * 0.3f;
  e.ori += e.omega * dt;
  if (e.ori > PI)
    e.ori -= 2.f * PI;
  else if (e.ori < -PI)
    e.ori += 2.f * PI;
  e.x += e.vx * dt;
  e.y += e.vy * dt;

  e.x = tile_val(e.x, worldSize);
  e.y = tile_val(e.y, worldSize);
}

