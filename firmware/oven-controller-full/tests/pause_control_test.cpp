#include "../src/pause_control.h"
#include "../src/pid_control.h"
#include "../src/output_supervisor.h"
#include "../src/cycle_report.h"
#include <cassert>
#include <cstdio>

int main() {
  regulation::ProgramPause pause;
  profile::Step ramp{profile::Type::Ramp, 175, 2, 0};
  float origin = 100;
  uint32_t segmentAt = 1000, before = 0;
  assert(!pause.begin(NAN, 61000, segmentAt, before));
  assert(!pause.begin(415, 61000, segmentAt, before));
  assert(pause.begin(101.5f, 61000, segmentAt, before));
  assert(pause.active && pause.targetC == 101.5f && pause.frozenStepMs == 60000);
  assert(!pause.begin(120, 70000, segmentAt, before));

  // Active PID can maintain the captured mean in pause, retaining compensation.
  regulation::Pid pid; pid.integral = 9;
  const float output = pid.update(pause.targetC, 101.5f, 12, .02f, 50, 60, 65000);
  assert(output == 9 && pid.integral == 9);
  heater::Plan plan; plan.cycleActive = plan.heatingAllowed = plan.healthy = true;
  plan.sampleAt = plan.cycleAt = plan.windowAt = 65000;
  plan.windowMs = 5000; plan.duty = output;
  OutputSupervisor supervisor; assert(supervisor.publish(plan));
  supervisor.tick(65000, true, false); assert(supervisor.relay);
  supervisor.tick(68000, true, false); assert(!supervisor.relay && supervisor.guard.fault == heater::Fault::StaleControl);
  supervisor.guard.clear(); supervisor.publish(plan); supervisor.emergencyStop();
  supervisor.tick(65000, true, false); assert(!supervisor.relay);
  supervisor.acknowledgeEmergency(); supervisor.blockRecovery(); supervisor.publish(plan);
  supervisor.tick(65000, true, false); assert(!supervisor.relay);

  // Resume from the held temperature, preserving active time but excluding pause.
  assert(pause.resume(ramp, 181000, origin, segmentAt, before));
  assert(origin == 101.5f && segmentAt == 181000 && before == 60000 && !pause.active);
  assert(profile::setpoint(ramp, origin, 0) == 101.5f);
  assert(profile::setpoint(ramp, origin, 60000) == 103.5f);
  assert(before + uint32_t(241000 - segmentAt) == 120000);
  assert(pid.integral == 9); // No reset at either program transition.
  assert(pause.begin(103.2f, 241000, segmentAt, before));
  assert(pause.frozenStepMs == 120000);
  assert(pause.resume(ramp, 301000, origin, segmentAt, before) && origin == 103.2f && before == 120000);

  // Hold qualification does not grow during pause; its original target resumes.
  profile::HoldTimer hold;
  hold.reset(0); hold.update(1000, true); hold.update(2000, true);
  assert(hold.elapsedMs == 1000);
  hold.update(3000, false, false); hold.update(60000, false, false);
  assert(hold.elapsedMs == 1000);
  hold.update(61000, true); assert(hold.elapsedMs == 1000);
  hold.update(62000, true); assert(hold.elapsedMs == 2000);
  profile::Step maintenance{profile::Type::Hold, 175, 0, 10};
  assert(pause.begin(174, 350000, segmentAt, before));
  assert(pause.resume(maintenance, 360000, origin, segmentAt, before));
  assert(profile::setpoint(maintenance, origin, 0) == 175);

  // Cooldown resumes a descending reference, without raising it to the old reference.
  profile::Step cooldown{profile::Type::Cooldown, 25, 1, 0};
  assert(pause.begin(90, 370000, segmentAt, before));
  assert(pause.resume(cooldown, 390000, origin, segmentAt, before));
  assert(origin == 90 && profile::setpoint(cooldown, origin, 60000) == 89);
  plan.heatingAllowed = false; plan.duty = pid.update(origin, origin + 1, 12, .02f, 50, 60, 65000, true);
  assert(plan.duty == 0); OutputSupervisor passive; passive.publish(plan);
  passive.tick(65000, true, false); assert(!passive.relay);

  // Bounds, wrap, STOP, restart and prolonged pauses remain supported.
  pause.clear(); assert(!pause.active && !std::isfinite(pause.targetC));
  assert(pause.begin(403, 400000, segmentAt, before) && pause.targetC == 400);
  pause.clear(); assert(pause.begin(-5, 400000, segmentAt, before) && pause.targetC == 0);
  pause.clear(); const uint32_t start = UINT32_MAX - 100;
  assert(pause.begin(100, uint32_t(start + 500), start, 1000) && pause.frozenStepMs == 1500);
  assert(pause.resume(ramp, uint32_t(start + 1000), origin, segmentAt, before));
  assert(before == 1500 && uint32_t(uint32_t(start + 1100) - segmentAt) == 100);
  regulation::ProgramPause afterBoot; assert(!afterBoot.active);
  heater::Guard timeout; plan.heatingAllowed = true; plan.duty = 30; plan.cycleAt = 0;
  plan.sampleAt = 13UL * 3600000; plan.windowAt = plan.sampleAt; timeout.publish(plan);
  assert(timeout.tick(plan.sampleAt) && timeout.fault == heater::Fault::None);
  puts("Pause: PID hold, continuous resume/rate, active times, hold qualification, cooldown, bounds, STOP, safety and wrap passed");
}
