#include "entity.h"

#include "mathUtils.h"

void SimulateEntity(Entity& e, float dt) {
  bool const is_braking = Sign(e.thr) != 0.f && Sign(e.thr) != Sign(e.speed);
  float const accel = is_braking ? 12.f : 3.f;
  e.speed = MoveTo(e.speed, Clamp(e.thr, -0.3, 1.f) * 10.f, dt, accel);
  e.ori += e.steer * dt * Clamp(e.speed, -2.f, 2.f) * 0.3f;
  e.ori = e.ori + (e.ori > kPi ? -2.f * kPi : e.ori < -kPi ? 2.f * kPi : 0.f);
  e.x += cosf(e.ori) * e.speed * dt;
  e.y += sinf(e.ori) * e.speed * dt;
}
