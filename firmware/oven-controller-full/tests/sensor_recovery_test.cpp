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
  assert(recovery.verify(Confirmation::Safe, true, inhibit, safeRead) == RecoveryResult::NotAttempted);
  assert(recovery.verify(Confirmation::Invalid, false, inhibit, safeRead) == RecoveryResult::NotAttempted);
  assert(reads == 0 && inhibited == 0);
  assert(recovery.verify(Confirmation::Invalid, true, inhibit, safeRead) == RecoveryResult::Confirmed);
  assert(reads == 1 && inhibited == 1 && !outputs.relay);
  outputs.endSensorCheck(); outputs.tick(200, true, false); assert(!outputs.relay);
  // Recovery clears no alarm and drives no output itself. A fresh healthy
  // control plan can restore heat after that single confirmation.
  plan.sampleAt = 200;
  assert(outputs.publish(plan)); outputs.tick(200, true, false); assert(outputs.relay);
  // Isolated recovered events may repeat: no one-minute lockout remains.
  for (unsigned i = 0; i < 100; ++i) {
    assert(recovery.verify(Confirmation::Invalid, true, inhibit, safeRead) == RecoveryResult::Confirmed);
    outputs.endSensorCheck();
  }
  assert(reads == 101);
  // Original fault plus eight further faults can still recover: only nine in a row.
  reads = 0;
  assert(recovery.verify(Confirmation::Invalid, true, inhibit, [&] {
    assert(!outputs.relay);
    return ++reads <= 8 ? Confirmation::Invalid : Confirmation::Safe;
  }) == RecoveryResult::Confirmed);
  assert(reads == 9);
  outputs.endSensorCheck();
  reads = 0;
  assert(recovery.verify(Confirmation::Invalid, true, inhibit, [&] { ++reads; return Confirmation::Invalid; }) == RecoveryResult::Failed);
  assert(reads == 9 && recovery.failure == probes::RecoveryFailure::ConsecutiveFaults);
  outputs.endSensorCheck();
  // A single fresh safe confirmation ends recovery immediately: no extra read.
  reads = 0;
  assert(recovery.verify(Confirmation::Invalid, true, inhibit, [&] {
    ++reads; return reads == 1 || reads >= 10 ? Confirmation::Safe : Confirmation::Invalid;
  }) == RecoveryResult::Confirmed);
  assert(reads == 1);
  outputs.endSensorCheck();
  reads = 0;
  assert(recovery.verify(Confirmation::Invalid, true, inhibit, [&] {
    return ++reads % 2 ? Confirmation::Safe : Confirmation::Invalid;
  }) == RecoveryResult::Confirmed);
  assert(reads == 1 && recovery.failure == probes::RecoveryFailure::None);
  outputs.endSensorCheck();
  reads = 0;
  assert(recovery.verify(Confirmation::Invalid, true, inhibit, [&] {
    ++reads; return Confirmation::Unsafe; // Trustworthy temperature at/above the hard limit.
  }) == RecoveryResult::Failed);
  assert(reads == 1 && recovery.failure == probes::RecoveryFailure::Unsafe);
  outputs.endSensorCheck();
  // TEST stays inhibited across GPIO ticks; recovery itself cannot extend the pulse.
  assert(outputs.startTest(TestMode::RelayPulse, 500, 1000, false));
  outputs.tick(500, true, false); assert(outputs.relay);
  reads = 0;
  assert(recovery.verify(Confirmation::Invalid, true, inhibit, [&] {
    outputs.tick(600, true, false); assert(!outputs.relay && !outputs.red && !outputs.green);
    return ++reads < 4 ? Confirmation::Invalid : Confirmation::Safe;
  }) == RecoveryResult::Confirmed);
  outputs.endSensorCheck(); outputs.tick(700, true, false); assert(outputs.relay);
  outputs.beginSensorCheck();
  assert(outputs.tick(1500, true, false) == TestEvent::Completed);
  outputs.endSensorCheck(); outputs.tick(1501, true, false); assert(!outputs.relay);
  // A failed check cannot leave a diagnostic pulse waiting to restart.
  assert(outputs.startTest(TestMode::RelayPulse, 2000, 1000, false));
  assert(recovery.verify(Confirmation::Invalid, true, inhibit, [] { return Confirmation::Unsafe; }) == RecoveryResult::Failed);
  outputs.stopTest(true); outputs.endSensorCheck(); outputs.tick(2100, true, true);
  assert(!outputs.relay && !outputs.diagnostic && outputs.test.mode == TestMode::Idle);
  outputs.guard.fault = heater::Fault::StaleControl;
  assert(recovery.verify(Confirmation::Invalid, true, inhibit, safeRead) == RecoveryResult::Confirmed);
  outputs.endSensorCheck(); outputs.publish(plan); outputs.tick(100, true, false);
  assert(!outputs.relay && outputs.guard.fault == heater::Fault::StaleControl);
  // Every possible nonzero status, including 0x0c and changing combinations,
  // is retried with outputs OFF; none becomes a valid temperature.
  for (unsigned fault = 1; fault <= 255; ++fault) {
    for (bool diagnostic : {false, true}) {
      probes::PairRecovery pair;
      const auto bad = probes::confirmation(fault, 11678, 175, diagnostic);
      assert(bad == Confirmation::Invalid);
      assert(pair.observe(bad, Confirmation::Safe) == RecoveryResult::Pending);
      assert(pair.observe(Confirmation::Safe, Confirmation::Safe) == RecoveryResult::Confirmed);
      probes::PairRecovery both;
      assert(both.observe(bad, bad) == RecoveryResult::Pending);
      assert(both.observe(Confirmation::Safe, bad) == RecoveryResult::Pending);
      assert(both.observe(Confirmation::Safe, Confirmation::Safe) == RecoveryResult::Confirmed);
    }
  }
  assert(probes::confirmation(0, 0, 25) == Confirmation::Invalid);
  assert(probes::confirmation(0, 32767, 25) == Confirmation::Invalid);
  assert(probes::confirmation(0, 11678, 175, false, true) == Confirmation::Invalid);
  assert(probes::confirmation(0, 8303, NAN) == Confirmation::Invalid);
  assert(probes::confirmation(0, 8303, INFINITY) == Confirmation::Invalid);
  assert(probes::confirmation(0, 8303, -21) == Confirmation::Invalid);
  assert(probes::confirmation(0, 8303, -21, true) == Confirmation::Safe);
  assert(probes::confirmation(0, 8303, 414.99f) == Confirmation::Safe);
  assert(probes::confirmation(0, 8303, 415) == Confirmation::Unsafe);
  assert(probes::confirmation(0, 8303, 450) == Confirmation::Unsafe);
  // A numerical value carrying a fault is untrusted, not a confirmed overtemperature.
  assert(probes::confirmation(0x0c, 11678, 450) == Confirmation::Invalid);
  probes::PairRecovery normal;
  assert(normal.observe(Confirmation::Safe, Confirmation::Safe) == RecoveryResult::NotAttempted);
  // A valid reading from only one probe is insufficient. The initially healthy
  // sibling can fail, and neither probe may be frozen while the other retries.
  probes::PairRecovery pair;
  assert(pair.observe(Confirmation::Invalid, Confirmation::Safe) == RecoveryResult::Pending);
  assert(pair.observe(Confirmation::Safe, Confirmation::Invalid) == RecoveryResult::Pending);
  assert(pair.observe(Confirmation::Invalid, Confirmation::Safe) == RecoveryResult::Pending);
  assert(pair.observe(Confirmation::Safe, Confirmation::Safe) == RecoveryResult::Confirmed);
  for (unsigned first = 0; first != 2; ++first) {
    probes::PairRecovery stuck;
    for (unsigned count = 0; count < 10; ++count) {
      const auto bad = probes::confirmation(uint8_t(4 << (count % 6)), 8303, 25);
      const auto result = first ? stuck.observe(Confirmation::Safe, bad) : stuck.observe(bad, Confirmation::Safe);
      assert(result == (count == 9 ? RecoveryResult::Failed : RecoveryResult::Pending));
    }
    assert(stuck.probe[first].failure == probes::RecoveryFailure::ConsecutiveFaults);
    assert(stuck.observe(Confirmation::Safe, Confirmation::Safe) == RecoveryResult::Failed);
  }
  probes::PairRecovery alternating;
  assert(alternating.observe(Confirmation::Invalid, Confirmation::Safe) == RecoveryResult::Pending);
  for (unsigned count = 1; count <= probes::Recovery::kMaxConfirmationReads; ++count) {
    const auto result = count % 2 ? alternating.observe(Confirmation::Safe, Confirmation::Invalid) :
      alternating.observe(Confirmation::Invalid, Confirmation::Safe);
    assert(result == (count == probes::Recovery::kMaxConfirmationReads ? RecoveryResult::Failed : RecoveryResult::Pending));
  }
  probes::PairRecovery hot;
  assert(hot.observe(Confirmation::Invalid, Confirmation::Safe) == RecoveryResult::Pending);
  assert(hot.observe(Confirmation::Safe, Confirmation::Unsafe) == RecoveryResult::Failed);
  assert(hot.probe[1].failure == probes::RecoveryFailure::Unsafe);
  std::cout << "OK: all 255 fault masks, single fresh safe pair recovery, invalid RAW/temperatures, changing faults, ten-error limit, unstable/hot rejection, PID/TEST inhibition and fault latches\n";
}
