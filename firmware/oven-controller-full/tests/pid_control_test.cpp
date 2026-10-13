#include "../src/pid_control.h"
#include "../src/thermal_limits.h"
#include "../src/output_supervisor.h"
#include <cassert>
#include <cstdio>

int main() {
  using regulation::Pid;
  // Regression from the live oven: a hot probe above 176 C must not erase I
  // while the mean is still below the 175 C target. Safety checks use hottest
  // independently; this controller regulates the mean.
  Pid p; p.integral = 18;
  const float first = 176.3f, second = 172.0f;
  const float mean = (first + second) * 0.5f;
  assert(p.update(175, mean, 12, .02f, 50, 60, 100) > 18);
  assert(p.integral > 18 && !p.meanLimited);

  // A temporary mean +1 C inhibition preserves compensation and resumes
  // naturally when the mean returns to target, without a derivative kick.
  p.reset(); p.integral = 20;
  assert(p.update(175, 176, 12, .02f, 0, 60, 100) == 0);
  assert(p.meanLimited && p.integral > 19.9f);
  assert(p.update(175, 175, 12, .02f, 0, 60, 600) > 19.9f);
  assert(!p.meanLimited);

  const profile::Step ramp{profile::Type::Ramp, 175, 10, 0};
  const profile::Step hold{profile::Type::Hold, 175, 0, 60};
  const profile::Step otherHold{profile::Type::Hold, 180, 0, 60};
  const profile::Step cool{profile::Type::Cooldown, 20, 2, 0};
  assert(regulation::continuousHold(ramp, hold));
  assert(regulation::continuousHold(hold, hold));
  assert(!regulation::continuousHold(ramp, otherHold));
  assert(!regulation::continuousHold(hold, ramp));
  assert(!regulation::continuousHold(hold, cool));
  assert(!regulation::continuousHold(cool, hold));
  // The production step transition calls reset only for discontinuous steps.
  p.reset(); p.integral = 18;
  p.update(175, 175, 12, .02f, 50, 60, 100);
  if (!regulation::continuousHold(ramp, hold)) p.reset();
  assert(p.update(175, 175, 12, .02f, 50, 60, 600) == 18);
  assert(p.derivative == 0);
  if (!regulation::continuousHold(hold, cool)) p.reset();
  assert(p.integral == 0 && p.output == 0);

  // Upper/lower saturation do not wind up the integral. Zero-gain cases and
  // a changed power limit stay bounded, including the stored compensation.
  p.reset();
  for (uint32_t t = 0; t < 60000; t += 500) {
    assert(p.update(175, 150, 12, .02f, 0, 30, t) == 30);
    assert(p.integral == 0);
  }
  p.integral = 10;
  assert(p.update(175, 190, 12, .02f, 0, 60, 61000) == 0);
  assert(p.integral == 10);
  p.reset(); p.integral = 60;
  assert(p.update(175, 175, 0, 0, 0, 30, 0) == 30 && p.integral == 30);
  p.reset(); assert(p.update(175, 175, 0, 0, 0, 60, 0) == 0);

  // D is on the measurement: a target change alone creates no derivative kick.
  p.reset(); p.update(175, 174, 12, .02f, 50, 60, 100);
  p.update(180, 174, 12, .02f, 50, 60, 600); assert(p.derivative == 0);
  p.reset();
  const uint32_t start = UINT32_MAX - 100;
  p.update(175, 174, 0, .02f, 0, 60, start);
  p.update(175, 174, 0, .02f, 0, 60, uint32_t(start + 500));
  assert(fabsf(p.integral - .02f) < .00001f);
  p.update(NAN, 174, 12, .02f, 50, 60, 600);
  assert(!p.output && !p.integral);
  assert(!p.update(175, NAN, 12, .02f, 50, 60, 1100));

  // Retained PID memory cannot bypass physical output supervision or faults.
  OutputSupervisor outputs;
  heater::Plan plan; plan.cycleActive = plan.heatingAllowed = plan.healthy = true;
  plan.windowMs = 5000; plan.duty = 18;
  outputs.publish(plan); outputs.tick(0, true, false); assert(outputs.relay);
  outputs.emergencyStop(); outputs.publish(plan); outputs.tick(0, true, false); assert(!outputs.relay);
  outputs.acknowledgeEmergency();
  plan.healthy = thermal::healthyPair(100, true, 415, true);
  outputs.publish(plan); outputs.tick(0, true, false); assert(!outputs.relay);
  plan.healthy = true; outputs.publish(plan); outputs.tick(0, true, false); assert(!outputs.relay);
  puts("PID: continuous hold, mean cutoff, retained I, saturation, invalid input, timer wrap and output latches passed");
}
