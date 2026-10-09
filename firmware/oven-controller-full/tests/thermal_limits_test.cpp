#include "../src/thermal_limits.h"
#include "../src/profile.h"
#include "../src/heater_guard.h"
#include <cassert>
#include <limits>
#include <initializer_list>
#include <cstdio>

int main() {
  for (float c : {0.0f, 190.0f, 220.0f, 399.9f, 400.0f}) assert(thermal::validTarget(c));
  for (float c : {-0.1f, 400.1f, std::numeric_limits<float>::infinity(), NAN})
    assert(!thermal::validTarget(c));
  for (float c : {-20.0f, 205.0f, 220.0f, 400.0f, 415.0f, 430.0f})
    assert(thermal::validReading(c, false));
  assert(!thermal::validReading(-20.1f, false));
  assert(thermal::validReading(-50.0f, true));
  assert(!thermal::validReading(-50.1f, true));
  for (float c : {430.1f, std::numeric_limits<float>::infinity(), NAN}) {
    assert(!thermal::validReading(c, false));
    assert(!thermal::validReading(c, true));
  }
  assert(thermal::belowHardLimit(400.0f));
  assert(thermal::belowHardLimit(414.9f));
  assert(!thermal::belowHardLimit(415.0f));
  assert(!thermal::belowHardLimit(NAN));

  const profile::Step ramp{profile::Type::Ramp, 400.0f, 10.0f, 0};
  assert(profile::setpoint(ramp, 20.0f, 38 * 60000UL) == 400.0f);
  assert(profile::setpoint(ramp, 20.0f, 40 * 60000UL) == 400.0f);
  assert(profile::inBand(399.0f, 401.0f, 400.0f));

  // High readings stay measurable; an absolute cutoff still inhibits and latches the output.
  heater::Guard guard;
  heater::Plan plan;
  plan.cycleActive = plan.heatingAllowed = true;
  plan.duty = 30; plan.windowMs = 5000;
  plan.healthy = thermal::validReading(400, false) && thermal::belowHardLimit(400);
  guard.publish(plan); assert(guard.tick(0));
  plan.healthy = thermal::validReading(415, false) && thermal::belowHardLimit(415);
  guard.publish(plan); assert(!guard.tick(0));
  assert(guard.fault == heater::Fault::InvalidPlan);
  plan.healthy = true; guard.publish(plan); assert(!guard.tick(0));
  puts("OK: targets through 400 C, high-temperature profile, reading boundaries and absolute cutoff latch");
}
