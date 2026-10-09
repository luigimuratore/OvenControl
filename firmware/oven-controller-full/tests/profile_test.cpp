#include "../src/profile.h"
#include <cassert>
#include <cmath>
#include <initializer_list>

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

  // A warm or cool probe must not move the reference at the hold -> cooldown boundary.
  const Step hold50{Type::Hold, 50.0f, 0, 3};
  const Step cool25{Type::Cooldown, 25.0f, 1.0f, 0};
  const float previous = setpoint(hold50, 49.5f, 3 * 60000UL);
  for (float measured : {50.5f, 49.5f}) {
    const float start = startTemperature(cool25, measured, previous);
    assert(setpoint(cool25, start, 0) == previous);
    float last = previous;
    for (uint32_t ms = 0; ms <= 30 * 60000UL; ms += 500) {
      const float next = setpoint(cool25, start, ms);
      assert(next <= last && next >= cool25.target);
      last = next;
    }
    assert(setpoint(cool25, start, 60000UL) == 49.0f);
    assert(setpoint(cool25, start, 25 * 60000UL) == 25.0f);
    assert(!cooldownDone(cool25, start, 25 * 60000UL, 26.1f));
    assert(cooldownDone(cool25, start, 25 * 60000UL, 26.0f));
  }
  // Direct cooldown starts at the actual temperature; heating ramps keep their existing start.
  assert(startTemperature(cool25, 50.5f) == 50.5f);
  assert(startTemperature(ramp, 50.5f, 50.0f) == 50.5f);
  // Successive cooldowns continue from the preceding reference as well.
  assert(startTemperature(cool25, 40.5f, setpoint(cool, 80.0f, 40 * 60000UL)) == 40.0f);
  assert(inBand(45.0f, 55.0f, 50.0f));
  assert(!inBand(44.99f, 50.0f, 50.0f));
  assert(!inBand(50.0f, 55.01f, 50.0f));
  assert(!inBand(NAN, 50.0f, 50.0f));
  assert(remainingSec(hold, 40, 1000, 590000, 80, 80) == 10);
  assert(remainingSec(hold, 40, 1000, 600000, 80, 80) == 0);
  assert(remainingSec(ramp, 20, 15 * 60000UL, 0, 49, 51) == 900);
  assert(remainingSec(ramp, 20, 25 * 60000UL, 0, 65, 66) == 420);
  assert(remainingSec(ramp, 20, 30 * 60000UL, 0, 79, 80) == 0);
  assert(remainingSec(cool, 80, 20 * 60000UL, 0, 64, 65) == 1440);
  assert(remainingSec(cool, 80, 40 * 60000UL, 0, 40, 41) == 0);
  assert(std::isnan(remainingSec(cool, 80, 0, 0, NAN, 65)));
}
