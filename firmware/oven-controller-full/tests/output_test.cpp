#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <initializer_list>
#include "../src/output_test.h"

int main() {
  OutputTest test;
  assert(!test.relay && test.mode == TestMode::Idle);
  assert(!test.heartbeat(0));
  test.tick(0, true); assert(test.green && !test.red && !test.relay);
  assert(!test.start(TestMode::Idle, 0));
  for (uint32_t ms : {0U, 99U, 5001U, 9999U, 30001U, 60001U, 120001U, 599999U, 600001U, UINT32_MAX}) {
    assert(!test.start(TestMode::RelayPulse, 0, ms));
    assert(test.mode == TestMode::Idle && !test.relay);
  }

  // A command starts directly, with no manual arming or 60-second deadline.
  const uint32_t durations[] = {100, 500, 1000, 2000, 5000, 30000, 60000, 120000, 600000};
  for (uint32_t ms : durations) {
    const uint32_t start = 1000000;
    assert(test.start(TestMode::RelayPulse, start, ms));
    assert(test.relay && test.red && test.remaining(start) == ms);
    for (uint32_t elapsed = 100; elapsed < ms; elapsed += 100) {
      assert(test.heartbeat(start + elapsed));
      assert(test.tick(start + elapsed, true) == TestEvent::None);
      assert(test.relay && test.red && test.remaining(start + elapsed) == ms - elapsed);
      assert(!test.start(TestMode::LedRed, start + elapsed));
    }
    assert(test.tick(start + ms - 1, true) == TestEvent::None && test.relay);
    assert(test.remaining(start + ms - 1) == 1);
    assert(test.tick(start + ms, true) == TestEvent::Completed);
    assert(test.mode == TestMode::Idle && !test.relay && !test.red && test.green);
    assert(!test.heartbeat(start + ms + 1));
    assert(test.start(TestMode::RelayPulse, start + ms + 1, ms));
    test.stop(); assert(test.mode == TestMode::Idle && !test.relay && !test.red);
    assert(!test.heartbeat(start + ms + 2));
  }

  assert(test.start(TestMode::RelayPulse, 1000, 600000));
  assert(test.tick(3499, true) == TestEvent::None && test.relay);
  assert(test.tick(3500, true) == TestEvent::ConnectionLost);
  assert(test.mode == TestMode::Idle && !test.relay);
  assert(!test.heartbeat(3501)); // Heartbeat cannot restart an expired test.
  assert(test.start(TestMode::RelayPulse, 3502, 5000));
  test.stop();
  assert(test.start(TestMode::RelayPulse, 4000, 600000));
  assert(!test.heartbeat(6500)); // Late heartbeats cannot revive a lost lease.
  assert(!test.relay && test.mode == TestMode::Idle);

  assert(test.start(TestMode::RelaySequence, 5000));
  for (uint32_t elapsed = 0; elapsed < 9000; elapsed += 100) {
    assert(test.heartbeat(5000 + elapsed));
    test.tick(5000 + elapsed, true);
    assert(test.relay == (elapsed % 3000 < 1000));
    assert(test.red == test.relay);
  }
  assert(test.tick(14000, true) == TestEvent::Completed && !test.relay);

  assert(test.start(TestMode::LedSequence, 30000));
  for (uint32_t elapsed = 0; elapsed < 8000; elapsed += 100) {
    assert(test.heartbeat(30000 + elapsed)); test.tick(30000 + elapsed, true);
    assert(!test.relay);
    assert(test.green == (elapsed / 2000 == 1 || elapsed / 2000 == 3));
    assert(test.red == (elapsed / 2000 == 2 || elapsed / 2000 == 3));
  }
  assert(test.tick(38000, true) == TestEvent::Completed);

  const TestMode ledModes[] = {TestMode::LedGreen, TestMode::LedRed, TestMode::LedBoth, TestMode::LedOff};
  for (auto mode : ledModes) {
    assert(test.start(mode, 40000)); test.tick(40000, true);
    assert(!test.relay);
    assert(test.green == (mode == TestMode::LedGreen || mode == TestMode::LedBoth));
    assert(test.red == (mode == TestMode::LedRed || mode == TestMode::LedBoth));
    for (uint32_t elapsed = 500; elapsed < 3000; elapsed += 500) assert(test.heartbeat(40000 + elapsed));
    assert(test.tick(43000, true) == TestEvent::Completed && !test.relay);
    assert(test.start(mode, 43001));
    test.stop(); assert(test.mode == TestMode::Idle && !test.relay && !test.red);
  }

  // Timer arithmetic remains correct across the 32-bit millis() wrap.
  const uint32_t nearWrap = UINT32_MAX - 49;
  assert(test.start(TestMode::RelayPulse, nearWrap, 100));
  test.tick(49, true); assert(test.relay);
  assert(test.tick(50, true) == TestEvent::Completed && !test.relay);
  assert(test.start(TestMode::RelayPulse, nearWrap, 5000));
  assert(test.tick(uint32_t(nearWrap + OutputTest::LEASE_MS), true) == TestEvent::ConnectionLost);
  assert(!test.relay && test.mode == TestMode::Idle);

  const uint32_t longWrap = UINT32_MAX - 39999;
  assert(test.start(TestMode::RelayPulse, longWrap, 600000));
  for (uint32_t elapsed = 500; elapsed < 600000; elapsed += 500) {
    assert(test.heartbeat(uint32_t(longWrap + elapsed)));
    assert(test.relay && test.red);
    assert(test.remaining(uint32_t(longWrap + elapsed)) == 600000 - elapsed);
  }
  assert(test.tick(uint32_t(longWrap + 599999), true) == TestEvent::None && test.relay);
  assert(test.tick(uint32_t(longWrap + 600000), true) == TestEvent::Completed);
  assert(!test.relay && test.mode == TestMode::Idle);

  puts("OK: direct start, boot/STOP/restart, short and long relay deadlines, LED/relay sequences, lease, millis wrap");
}
