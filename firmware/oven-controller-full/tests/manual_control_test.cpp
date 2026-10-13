#include "../src/manual_control.h"
#include "../src/output_supervisor.h"
#include "../src/power_recovery.h"
#include <cassert>
#include <cstring>
#include <cstdio>

struct Store {
  recovery::Record record;
  bool present = false, fail = false;
  int load(recovery::Record &out) { if (!present) return 0; out = record; return 1; }
  bool save(const recovery::Record &value) { if (fail) return false; record = value; present = true; return true; }
};
int main() {
  manual::Session s;
  assert(!s.active() && !s.heatingAllowed());
  assert(!s.start(NAN, 60, 20, 20, 0) && !s.start(400.1f, 60, 20, 20, 0));
  assert(!s.start(-.1f, 60, 20, 20, 0) && !s.start(50, 60, 415, 415, 0));
  assert(!s.changeTarget(50, 60, 20, 20, 0));
  assert(s.start(175, 60, 100, 100, 100));
  assert(s.active() && s.heatingAllowed() && s.startedAt == 100);
  assert(!s.start(180, 60, 100, 100, 200));
  assert(s.changeTarget(180, 60, 174, 174, 10100));
  assert(s.startedAt == 100 && s.elapsedMs(10100) == 10000 && s.heatingAllowed());
  assert(!s.changeTarget(NAN, 60, 174, 174, 11100) && s.target == 180);
  assert(s.changeTarget(150, 60, 176, 176, 20100));
  assert(s.coolingToTarget && !s.heatingAllowed());
  s.tick(160, 30100); assert(!s.heatingAllowed());
  s.tick(151, 40100); assert(!s.heatingAllowed()); // Reference has not reached the new target yet.
  s.tick(150, 50100); assert(s.heatingAllowed() && !s.coolingToTarget);
  assert(s.cooldown(25, 60, 50100)); assert(s.active() && !s.heatingAllowed());
  s.tick(30, 60100); assert(s.mode == manual::Mode::Cooldown && !s.heatingAllowed());
  assert(s.changeTarget(160, 60, 30, 30, 70100) && s.heatingAllowed());
  s.stop(80100); assert(!s.active() && !s.heatingAllowed() && s.elapsedMs(90100) == 80000);

  // A 2 C/min ramp advances the reference, clamps at the target and holds indefinitely.
  manual::Session ramp;
  assert(!ramp.start(50, .09f, 20, 20, 0) && !ramp.start(50, NAN, 20, 20, 0));
  assert(ramp.start(50, 2, 20, 20, 0));
  ramp.tick(22, 60000); assert(ramp.reference == 22);
  assert(ramp.changeTarget(60, 4, 22, 22, 60000) && ramp.reference == 22);
  ramp.tick(26, 120000); assert(ramp.reference == 26);
  ramp.tick(60, 720000); assert(ramp.reference == 60 && ramp.heatingAllowed());
  ramp.tick(60, 43200000); assert(ramp.active() && ramp.reference == 60);
  assert(ramp.cooldown(25, 2, 43200000) && ramp.reference == 60 && !ramp.heatingAllowed());
  ramp.tick(55, 43260000); assert(ramp.reference == 58 && !ramp.heatingAllowed());
  ramp.tick(20, 45000000); assert(ramp.reference == 25 && !ramp.heatingAllowed());
  assert(ramp.changeTarget(100, 2, 20, 20, 45000000));
  assert(ramp.reference == 20 && ramp.heatingAllowed());
  ramp.tick(22, 45060000); assert(ramp.reference == 22);

  // Lowering a target while its old reference is ahead of the oven never reheats
  // towards that old reference. Both reference and actual must reach the new band.
  manual::Session down; assert(down.start(100, 60, 20, 20, 0));
  down.tick(70, 80000); assert(down.reference == 100);
  assert(down.changeTarget(80, 1, 70, 70, 80000) && !down.heatingAllowed());
  down.tick(70, 140000); assert(down.reference == 99 && !down.heatingAllowed());
  down.tick(70, 1280000); assert(down.reference == 80 && down.heatingAllowed());
  manual::Session precise; assert(precise.start(400, .1f, 398, 398, 0));
  for (uint32_t second = 1; second <= 500; ++second) precise.tick(398, second * 1000);
  assert(fabsf(precise.reference - (398 + 500 * .1f / 60)) < .0001f);

  // Manual maintenance has no program timer; health/freshness and the program
  // 12-hour timeout continue to operate independently.
  const uint32_t later = 13UL * 3600000;
  heater::Plan plan;
  plan.cycleActive = plan.healthy = plan.heatingAllowed = plan.manual = true;
  plan.windowMs = 5000; plan.duty = 60; plan.sampleAt = later;
  heater::Guard manualGuard; manualGuard.publish(plan); assert(manualGuard.tick(later));
  assert(manualGuard.fault == heater::Fault::None);
  plan.manual = false; heater::Guard programGuard; programGuard.publish(plan);
  assert(!programGuard.tick(later) && programGuard.fault == heater::Fault::CycleTimeout);
  plan.manual = true; plan.healthy = false; manualGuard.publish(plan);
  assert(!manualGuard.tick(later) && manualGuard.fault == heater::Fault::InvalidPlan);
  heater::Guard stale; plan.healthy = true; stale.publish(plan);
  assert(!stale.tick(later + 3000) && stale.fault == heater::Fault::StaleControl);

  manual::Session wrap;
  const uint32_t start = UINT32_MAX - 100;
  assert(wrap.start(100, 60, 30, 30, start)); wrap.tick(30, uint32_t(start + 500));
  assert(wrap.elapsedMs(uint32_t(start + 1000)) == 1000);
  manual::Session longRun; assert(longRun.start(100, 60, 30, 30, 0));
  const uint32_t period = 25UL * 86400000;
  longRun.tick(30, period); longRun.tick(30, uint32_t(uint64_t(period) * 2));
  assert(longRun.elapsedMs(uint32_t(uint64_t(period) * 2)) == uint64_t(period) * 2);

  OutputSupervisor outputs;
  plan.healthy = true; plan.sampleAt = 0; outputs.publish(plan); outputs.tick(0, true, false); assert(outputs.relay);
  assert(!outputs.enterTest(true));
  outputs.emergencyStop(); outputs.publish(plan); outputs.tick(0, true, false); assert(!outputs.relay);
  outputs.acknowledgeEmergency(); outputs.blockRecovery(); outputs.publish(plan);
  outputs.tick(0, true, false); assert(!outputs.relay);

  Store store; recovery::Journal journal;
  assert(journal.begin(store, "Controllo manuale", 100));
  assert(!strcmp(store.record.program, "Controllo manuale"));
  recovery::Journal reboot; assert(reboot.load(store) && reboot.boot(store, false, 0, 1));
  assert(reboot.data.pending && !reboot.data.active);
  manual::Session afterBoot; assert(!afterBoot.active() && !afterBoot.heatingAllowed());
  store.fail = true; assert(!reboot.acknowledge(store) && reboot.data.pending);
  puts("Manual: targets, passive lowering/cooldown, indefinite hold, timers, STOP, faults, emergency and blackout latch passed");
}
