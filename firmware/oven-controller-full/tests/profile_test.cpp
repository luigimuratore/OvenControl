#include "../src/profile.h"
#include <cassert>
#include <cmath>

int main() {
  using namespace profile;
  const Step ramp{Type::Ramp, 80.0f, 2.0f, 0};
  assert(std::fabs(setpoint(ramp, 20.0f, 15 * 60000UL) - 50.0f) < 0.001f);
  assert(std::fabs(setpoint(ramp, 20.0f, 30 * 60000UL) - 80.0f) < 0.001f);
  assert(!rampDone(ramp, 20.0f, 30 * 60000UL, 78.9f));
  assert(rampDone(ramp, 20.0f, 30 * 60000UL, 79.0f));
  const Step fasterRamp{Type::Ramp, 80.0f, 10.0f, 0};
  assert(std::fabs(setpoint(fasterRamp, 20.0f, 3 * 60000UL) - 50.0f) < 0.001f);

  const Step hold{Type::Hold, 80.0f, 0, 10};
  assert(setpoint(hold, 40.0f, 5 * 60000UL) == 80.0f);

  const Step cool{Type::Cooldown, 40.0f, 1.0f, 0};
  assert(std::fabs(setpoint(cool, 80.0f, 20 * 60000UL) - 60.0f) < 0.001f);
  assert(!cooldownDone(cool, 80.0f, 20 * 60000UL, 40.0f));
  assert(!cooldownDone(cool, 80.0f, 40 * 60000UL, 41.1f));
  assert(cooldownDone(cool, 80.0f, 40 * 60000UL, 41.0f));
}
