#include <cassert>
#include <cstdint>
#include <iostream>
#include "../src/sensor_recovery.h"
#include "../src/output_supervisor.h"

int main() {
  using probes::RecoveryResult;
  probes::Recovery recovery;
  OutputSupervisor outputs;
  heater::Plan plan;
  plan.cycleActive = plan.heatingAllowed = plan.healthy = true;
  plan.sampleAt = plan.cycleAt = plan.windowAt = 100;
  plan.windowMs = 1000; plan.duty = 100;
  outputs.publish(plan); outputs.tick(100, true, false);
  assert(outputs.relay);
  int inhibited = 0, reads = 0;
  auto inhibit = [&] { ++inhibited; outputs.guard.inhibit(); outputs.tick(101, true, false); };
  auto safeRead = [&] { assert(!outputs.relay); ++reads; return true; };
  // No retry for ordinary readings, open/short faults, combined flags or TEST/idle.
  for (uint8_t fault : {uint8_t(0), uint8_t(0x08), uint8_t(0x24), uint8_t(0xff)})
    assert(recovery.verify(fault, 100, true, inhibit, safeRead) == RecoveryResult::NotAttempted);
  assert(recovery.verify(0x04, 100, false, inhibit, safeRead) == RecoveryResult::NotAttempted);
  assert(reads == 0 && inhibited == 0);
  assert(recovery.verify(0x04, 100, true, inhibit, safeRead) == RecoveryResult::Confirmed);
  assert(reads == 2 && inhibited == 1 && !outputs.relay);
  // Recovery itself cannot republish the old heating plan.
  outputs.tick(200, true, false); assert(!outputs.relay);
  assert(recovery.verify(0x04, 60099, true, inhibit, safeRead) == RecoveryResult::NotAttempted);
  assert(reads == 2);
  assert(recovery.verify(0x04, 60100, true, inhibit, safeRead) == RecoveryResult::Confirmed);
  assert(reads == 4);

  probes::Recovery persistent;
  reads = 0;
  assert(persistent.verify(0x04, 0, true, inhibit, [&] { ++reads; return false; }) == RecoveryResult::Failed);
  assert(reads == 1 && !outputs.relay);
  // One healthy conversion followed by another fault is not recovery.
  probes::Recovery intermittent;
  reads = 0;
  assert(intermittent.verify(0x04, 0, true, inhibit, [&] { return ++reads == 1; }) == RecoveryResult::Failed);
  assert(reads == 2 && !outputs.relay);
  // Each probe has its own budget, including near millis() rollover.
  probes::Recovery wrap;
  const uint32_t start = UINT32_MAX - 1000;
  assert(wrap.verify(0x04, start, true, inhibit, safeRead) == RecoveryResult::Confirmed);
  assert(wrap.verify(0x04, uint32_t(start + 59999), true, inhibit, safeRead) == RecoveryResult::NotAttempted);
  assert(wrap.verify(0x04, uint32_t(start + 60000), true, inhibit, safeRead) == RecoveryResult::Confirmed);
  // Fresh readings never clear a latched supervisor fault.
  outputs.guard.fault = heater::Fault::StaleControl;
  probes::Recovery latched;
  assert(latched.verify(0x04, 0, true, inhibit, safeRead) == RecoveryResult::Confirmed);
  outputs.publish(plan); outputs.tick(100, true, false);
  assert(!outputs.relay && outputs.guard.fault == heater::Fault::StaleControl);
  std::cout << "OK: isolated OV/UV, two confirmations, heater inhibited, persistent/recurrent faults, timer wrap, supervisor latch\n";
}
