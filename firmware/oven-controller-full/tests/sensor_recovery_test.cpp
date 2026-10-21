#include <cassert>
#include <iostream>
#include "../src/sensor_recovery.h"
#include "../src/output_supervisor.h"

int main() {
  using probes::RecoveryResult;
  using probes::Confirmation;
  probes::Recovery recovery;
  OutputSupervisor outputs;
  heater::Plan plan;
  plan.cycleActive = plan.heatingAllowed = plan.healthy = true;
  plan.sampleAt = plan.windowAt = 100;
  plan.windowMs = 1000; plan.duty = 100;
  outputs.publish(plan); outputs.tick(100, true, false);
  assert(outputs.relay);
  int inhibited = 0, reads = 0;
  auto inhibit = [&] { ++inhibited; outputs.beginSensorCheck(); assert(!outputs.relay); };
  auto safeRead = [&] { assert(!outputs.relay); ++reads; return Confirmation::Safe; };
  for (uint8_t fault : {uint8_t(0), uint8_t(0x08), uint8_t(0x24), uint8_t(0xff)})
    assert(recovery.verify(fault, true, inhibit, safeRead) == RecoveryResult::NotAttempted);
  assert(recovery.verify(0x04, false, inhibit, safeRead) == RecoveryResult::NotAttempted);
  assert(reads == 0 && inhibited == 0);
  assert(recovery.verify(0x04, true, inhibit, safeRead) == RecoveryResult::Confirmed);
  assert(reads == 2 && inhibited == 1 && !outputs.relay);
  outputs.endSensorCheck(); outputs.tick(200, true, false); assert(!outputs.relay);
  // Isolated recovered events may repeat: no one-minute lockout remains.
  for (unsigned i = 0; i < 100; ++i) {
    assert(recovery.verify(0x04, true, inhibit, safeRead) == RecoveryResult::Confirmed);
    outputs.endSensorCheck();
  }
  assert(reads == 202);
  // Original fault plus eight further faults can still recover: only nine in a row.
  reads = 0;
  assert(recovery.verify(0x04, true, inhibit, [&] {
    assert(!outputs.relay);
    return ++reads <= 8 ? Confirmation::OvUv : Confirmation::Safe;
  }) == RecoveryResult::Confirmed);
  assert(reads == 10);
  outputs.endSensorCheck();
  reads = 0;
  assert(recovery.verify(0x04, true, inhibit, [&] { ++reads; return Confirmation::OvUv; }) == RecoveryResult::Failed);
  assert(reads == 9 && recovery.failure == probes::RecoveryFailure::ConsecutiveFaults);
  outputs.endSensorCheck();
  // One good reading breaks the fault streak, but two good readings are required.
  reads = 0;
  assert(recovery.verify(0x04, true, inhibit, [&] {
    ++reads; return reads == 1 || reads >= 10 ? Confirmation::Safe : Confirmation::OvUv;
  }) == RecoveryResult::Confirmed);
  assert(reads == 11);
  outputs.endSensorCheck();
  reads = 0;
  assert(recovery.verify(0x04, true, inhibit, [&] {
    return ++reads % 2 ? Confirmation::Safe : Confirmation::OvUv;
  }) == RecoveryResult::Failed);
  assert(reads == probes::Recovery::kMaxConfirmationReads && recovery.failure == probes::RecoveryFailure::Unstable);
  outputs.endSensorCheck();
  reads = 0;
  assert(recovery.verify(0x04, true, inhibit, [&] {
    ++reads; return Confirmation::Unsafe; // Other fault, invalid RAW, nonfinite or unsafe temperature.
  }) == RecoveryResult::Failed);
  assert(reads == 1 && recovery.failure == probes::RecoveryFailure::Unsafe);
  outputs.endSensorCheck();
  // TEST stays inhibited across GPIO ticks; recovery itself cannot extend the pulse.
  assert(outputs.startTest(TestMode::RelayPulse, 500, 1000, false));
  outputs.tick(500, true, false); assert(outputs.relay);
  reads = 0;
  assert(recovery.verify(0x04, true, inhibit, [&] {
    outputs.tick(600, true, false); assert(!outputs.relay && !outputs.red && !outputs.green);
    return ++reads < 4 ? Confirmation::OvUv : Confirmation::Safe;
  }) == RecoveryResult::Confirmed);
  outputs.endSensorCheck(); outputs.tick(700, true, false); assert(outputs.relay);
  outputs.beginSensorCheck();
  assert(outputs.tick(1500, true, false) == TestEvent::Completed);
  outputs.endSensorCheck(); outputs.tick(1501, true, false); assert(!outputs.relay);
  // A failed check cannot leave a diagnostic pulse waiting to restart.
  assert(outputs.startTest(TestMode::RelayPulse, 2000, 1000, false));
  assert(recovery.verify(0x04, true, inhibit, [] { return Confirmation::Unsafe; }) == RecoveryResult::Failed);
  outputs.stopTest(true); outputs.endSensorCheck(); outputs.tick(2100, true, true);
  assert(!outputs.relay && !outputs.diagnostic && outputs.test.mode == TestMode::Idle);
  outputs.guard.fault = heater::Fault::StaleControl;
  assert(recovery.verify(0x04, true, inhibit, safeRead) == RecoveryResult::Confirmed);
  outputs.endSensorCheck(); outputs.publish(plan); outputs.tick(100, true, false);
  assert(!outputs.relay && outputs.guard.fault == heater::Fault::StaleControl);
  std::cout << "OK: repeated recoveries, 10 consecutive faults, two safe confirmations, unstable/unsafe rejection, PID and TEST inhibition, expiry and fault latch\n";
}
