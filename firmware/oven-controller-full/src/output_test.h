#pragma once
#include <stdint.h>

// Pure timing logic: no Arduino calls and no delays during output tests.
enum class TestMode { Idle, RelayPulse, RelaySequence, LedGreen, LedRed, LedBoth, LedOff, LedSequence };
enum class TestEvent { None, Completed, ConnectionLost };

class OutputTest {
 public:
  static constexpr uint32_t LEASE_MS = 2500;
  bool relay = false, red = false, green = false;
  TestMode mode = TestMode::Idle;

  static bool validRelayDuration(uint32_t ms) {
    return (ms >= 100 && ms <= 5000) || ms == 30000 || ms == 60000 || ms == 120000 || ms == 600000;
  }

  void stop() { relay = red = green = false; mode = TestMode::Idle; duration = 0; }
  bool heartbeat(uint32_t now) {
    tick(now, false);
    if (mode == TestMode::Idle) return false;
    heartbeatAt = now;
    return true;
  }
  bool start(TestMode requested, uint32_t now, uint32_t pulseMs = 1000) {
    tick(now, false);
    if (mode != TestMode::Idle || requested == TestMode::Idle) return false;
    if (requested == TestMode::RelayPulse && !validRelayDuration(pulseMs)) return false;
    mode = requested; startedAt = heartbeatAt = now;
    duration = requested == TestMode::RelayPulse ? pulseMs :
               requested == TestMode::RelaySequence ? 9000 :
               requested == TestMode::LedSequence ? 8000 : 3000;
    tick(now, false);
    return true;
  }
  TestEvent tick(uint32_t now, bool serverReady) {
    TestEvent event = TestEvent::None;
    if (mode != TestMode::Idle && uint32_t(now - heartbeatAt) >= LEASE_MS) { stop(); event = TestEvent::ConnectionLost; }
    else if (mode != TestMode::Idle && uint32_t(now - startedAt) >= duration) {
      mode = TestMode::Idle; event = TestEvent::Completed;
    }
    relay = red = false; green = serverReady;
    const uint32_t elapsed = uint32_t(now - startedAt);
    switch (mode) {
      case TestMode::RelayPulse: relay = red = true; break;
      case TestMode::RelaySequence: relay = red = elapsed % 3000 < 1000; break;
      case TestMode::LedGreen: green = true; break;
      case TestMode::LedRed: green = false; red = true; break;
      case TestMode::LedBoth: green = red = true; break;
      case TestMode::LedOff: green = false; break;
      case TestMode::LedSequence:
        green = elapsed / 2000 == 1 || elapsed / 2000 == 3;
        red = elapsed / 2000 == 2 || elapsed / 2000 == 3;
        break;
      default: break;
    }
    return event;
  }
  uint32_t remaining(uint32_t now) const {
    const uint32_t elapsed = uint32_t(now - startedAt);
    return mode == TestMode::Idle || elapsed >= duration ? 0 : duration - elapsed;
  }
 private:
  uint32_t heartbeatAt = 0, startedAt = 0, duration = 0;
};
