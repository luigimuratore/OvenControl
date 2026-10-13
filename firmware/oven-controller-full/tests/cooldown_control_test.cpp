#include "../src/pid_control.h"
#include "../src/manual_control.h"
#include "../src/output_supervisor.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

// A simple thermal plant exercises actual PID and SSR logic. It is deliberately
// not a prediction of the real oven's response or a replacement for commissioning.
void simulatedDescent(float rate, bool expectCorrection) {
  regulation::Pid pid; pid.integral = 6;
  OutputSupervisor supervisor;
  const profile::Step step{profile::Type::Cooldown, 25, rate, 0};
  float actual = 175, maxError = 0;
  unsigned corrections = 0;
  for (uint32_t now = 100; now <= 300000; now += 100) {
    const float reference = profile::setpoint(step, 175, now);
    const float duty = pid.update(reference, actual, 12, .02f, 50, 60, now, true);
    assert(std::isfinite(duty) && duty >= 0 && duty <= 60);
    if (actual >= reference) assert(duty == 0);
    corrections += duty > 0;
    heater::Plan plan; plan.cycleActive = plan.healthy = true;
    plan.heatingAllowed = actual < reference; plan.sampleAt = now;
    plan.windowMs = 5000; plan.duty = duty;
    assert(supervisor.publish(plan)); supervisor.tick(now, true, false);
    assert(supervisor.guard.fault == heater::Fault::None);
    if (actual >= reference) assert(!supervisor.relay);
    actual += ((supervisor.relay ? 1.4f : 0) - (actual - 25) / 1800) * .1f;
    maxError = fmaxf(maxError, fabsf(actual - reference));
  }
  if (expectCorrection) {
    const float passiveFinal = 25 + 150 * std::exp(-300 / 1800.0f);
    const float finalReference = profile::setpoint(step, 175, 300000);
    assert(corrections > 0 && maxError < 2);
    assert(fabsf(actual - finalReference) < fabsf(passiveFinal - finalReference) / 5);
  }
  else assert(corrections == 0 && actual > profile::setpoint(step, 175, 300000) + 8);
}

int main() {
  regulation::Pid pid; pid.integral = 30; pid.derivative = 5;
  for (float actual : {175.0f, 175.01f, 176.0f, 200.0f})
    assert(pid.update(175, actual, 12, .02f, 50, 60, 1000, true) == 0);
  pid.reset(); pid.integral = 6;
  assert(pid.update(175, 174, 12, .02f, 50, 60, 2000, true) > 0);
  assert(pid.integral > 0); // Gating above the descending reference does not erase compensation.
  simulatedDescent(1, true);  // Slower than the plant's natural cooling: add heat.
  simulatedDescent(10, false); // Faster than natural cooling: no heat, no relative-lag fault.

  profile::Step hold{profile::Type::Hold, 175, 0, 10}, cool{profile::Type::Cooldown, 50, 1, 0};
  assert(regulation::continuousControl(hold, cool));
  assert(regulation::continuousControl(cool, {profile::Type::Hold, 50, 0, 5}));
  assert(!regulation::continuousControl(cool, {profile::Type::Ramp, 100, 1, 0}));

  manual::Session manual;
  assert(manual.start(175, 1, 175, 175, 0) && manual.cooldown(50, 1, 0));
  manual.tick(160, 60000);
  assert(manual.descending() && manual.heatingAllowed() && manual.reference == 174);
  assert(!manual.cooldownComplete); // Falling below the curve before its end is corrected, not completion.
  manual.tick(60, 7500000); assert(manual.reference == 50 && !manual.cooldownComplete);
  manual.tick(50.8f, 7501000);
  assert(manual.cooldownComplete && !manual.heatingAllowed());
  manual.tick(70, 7502000); assert(!manual.heatingAllowed()); // Completion latches the output off.
  manual.stop(7503000); assert(!manual.active() && !manual.heatingAllowed());

  heater::Plan plan; plan.cycleActive = plan.heatingAllowed = plan.healthy = true;
  plan.duty = 30; plan.windowMs = 5000;
  OutputSupervisor outputs; outputs.publish(plan); outputs.tick(0, true, false); assert(outputs.relay);
  outputs.stopAll(); outputs.tick(0, true, false); assert(!outputs.relay);
  outputs.emergencyStop(); outputs.publish(plan); outputs.tick(0, true, false); assert(!outputs.relay);
  outputs.acknowledgeEmergency(); outputs.blockRecovery(); outputs.publish(plan);
  outputs.tick(0, true, false); assert(!outputs.relay);
  puts("Cooldown: slow-rate correction, fast natural cooling, zero heat at/above reference, PID continuity, endpoint and output latches passed");
}
