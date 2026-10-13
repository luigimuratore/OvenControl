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

  // Difference is diagnostic only: it cannot invalidate healthy probes or
  // create an InvalidPlan latch, including above both UI warning thresholds.
  heater::Plan different;
  different.cycleActive = different.heatingAllowed = true;
  different.windowMs = 5000; different.duty = 30;
  for (float delta : {10.0f, 10.1f, 20.0f, 20.1f, 70.0f}) {
    assert(thermal::healthyPair(100, true, 100 + delta, true));
    heater::Guard deltaGuard;
    different.healthy = thermal::healthyPair(100, true, 100 + delta, true);
    deltaGuard.publish(different);
    assert(deltaGuard.tick(0) && deltaGuard.fault == heater::Fault::None);
  }
  assert(!thermal::healthyPair(100, false, 120, true));
  assert(!thermal::healthyPair(100, true, 120, false));
  assert(!thermal::healthyPair(100, true, 415, true));
  assert(!thermal::healthyPair(415, true, 100, true));
  assert(!thermal::healthyPair(NAN, true, 100, true));
  assert(!thermal::healthyPair(100, true, NAN, true));

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
  puts("OK: targets through 400 C, diagnostic delta, invalid probes, reading boundaries and absolute cutoff latch");
}
