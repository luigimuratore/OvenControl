#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../src/output_test.h"

void alive(OutputTest &test, uint32_t from, uint32_t to) {
  for (uint32_t now = from; now <= to; now += 500) assert(test.heartbeat(now));
}

int main() {
  OutputTest test;
  assert(!test.armed && !test.relay);
  assert(!test.start(TestMode::RelayPulse, 0));
  test.tick(0, true); assert(test.green && !test.red && !test.relay);
  test.arm(100);
  assert(!test.start(TestMode::RelayPulse, 100, 99));
  assert(!test.start(TestMode::RelayPulse, 100, 5001));
  assert(test.start(TestMode::RelayPulse, 100, 100));
  assert(test.relay && test.red);
  assert(!test.start(TestMode::LedRed, 150));
  test.tick(199, true); assert(test.relay);
  assert(test.tick(200, true) == TestEvent::Completed);
  assert(!test.relay && !test.red && test.green);

  test.arm(1000); assert(test.start(TestMode::RelayPulse, 1000, 5000));
  assert(test.tick(3499, true) == TestEvent::None && test.relay);
  assert(test.tick(3500, true) == TestEvent::ConnectionLost);
  assert(!test.relay && !test.armed);
  assert(!test.heartbeat(3501)); // An expired session cannot be revived.

  test.arm(5000); assert(test.start(TestMode::RelaySequence, 5000));
  for (uint32_t elapsed = 0; elapsed < 9000; elapsed += 100) {
    assert(test.heartbeat(5000 + elapsed));
    test.tick(5000 + elapsed, true);
    assert(test.relay == (elapsed % 3000 < 1000));
    assert(test.red == test.relay);
  }
  assert(test.tick(14000, true) == TestEvent::Completed);
  assert(!test.relay);

  test.arm(15000); assert(test.start(TestMode::RelayPulse, 15000, 5000));
  alive(test, 15500, 19500);
  assert(test.tick(19999, true) == TestEvent::None && test.relay);
  assert(test.tick(20000, true) == TestEvent::Completed && !test.relay);

  test.arm(30000); assert(test.start(TestMode::LedSequence, 30000));
  for (uint32_t elapsed = 0; elapsed < 8000; elapsed += 100) {
    assert(test.heartbeat(30000 + elapsed)); test.tick(30000 + elapsed, true);
    assert(!test.relay);
    assert(test.green == (elapsed / 2000 == 1 || elapsed / 2000 == 3));
    assert(test.red == (elapsed / 2000 == 2 || elapsed / 2000 == 3));
  }
  assert(test.tick(38000, true) == TestEvent::Completed);

  const TestMode ledModes[] = {TestMode::LedGreen, TestMode::LedRed, TestMode::LedBoth, TestMode::LedOff};
  for (auto mode : ledModes) {
    test.arm(40000); assert(test.start(mode, 40000)); test.tick(40000, true);
    assert(!test.relay);
    assert(test.green == (mode == TestMode::LedGreen || mode == TestMode::LedBoth));
    assert(test.red == (mode == TestMode::LedRed || mode == TestMode::LedBoth));
    test.stop(); assert(!test.armed && !test.relay && !test.red);
    assert(!test.start(TestMode::RelayPulse, 40000));
  }

  test.arm(50000); alive(test, 50500, 109500);
  assert(test.start(TestMode::RelayPulse, 109500, 5000));
  assert(test.tick(110000, true) == TestEvent::ArmExpired);
  assert(!test.armed && !test.relay);

  // Timer arithmetic remains correct across the 32-bit millis() wrap.
  const uint32_t nearWrap = UINT32_MAX - 49;
  test.arm(nearWrap); assert(test.start(TestMode::RelayPulse, nearWrap, 100));
  test.tick(49, true); assert(test.relay);
  assert(test.tick(50, true) == TestEvent::Completed && !test.relay);
  test.arm(nearWrap); assert(test.start(TestMode::RelayPulse, nearWrap, 5000));
  assert(test.tick(uint32_t(nearWrap + OutputTest::LEASE_MS), true) == TestEvent::ConnectionLost);
  assert(!test.relay && !test.armed);

  puts("OK: boot/STOP, pulse deadlines, relay and LED sequences, lease, arm expiry, millis wrap");
}
