#pragma once
#include "heater_guard.h"
#include "output_test.h"

// One owner for all three GPIOs. Caller serializes access on the ESP.
class OutputSupervisor {
 public:
  heater::Guard guard;
  OutputTest test;
  bool diagnostic = false, updating = false, emergency = false;
  bool relay = false, red = false, green = false;

  bool enterTest(bool cycleActive) {
    if (cycleActive || updating || emergency) return false;
    guard.inhibit(); diagnostic = true;
    return true;
  }
  bool startTest(TestMode mode, uint32_t now, uint32_t duration, bool cycleActive) {
    if (cycleActive || updating || mode == TestMode::Idle ||
        (mode == TestMode::RelayPulse && !OutputTest::validRelayDuration(duration))) return false;
    if (!enterTest(cycleActive)) return false;
    return test.start(mode, now, duration);
  }
  bool publish(const heater::Plan &plan) {
    if (diagnostic || updating || emergency) { guard.inhibit(); return false; }
    guard.publish(plan); return true;
  }
  void stopTest(bool leave = false) {
    test.stop(); relay = red = green = false;
    if (leave) diagnostic = false;
  }
  void stopAll() {
    guard.inhibit(); stopTest();
  }
  void emergencyStop() { emergency = true; stopAll(); diagnostic = false; }
  void acknowledgeEmergency() { stopAll(); emergency = false; }
  void beginUpdate() {
    updating = true; stopAll(); diagnostic = false;
  }
  TestEvent tick(uint32_t now, bool serverReady, bool alarm) {
    if (updating || emergency) { stopAll(); return TestEvent::None; }
    if (diagnostic) {
      guard.inhibit();
      const auto event = test.tick(now, serverReady);
      relay = test.relay; red = test.red; green = test.green;
      return event;
    }
    relay = guard.tick(now); red = relay; green = serverReady && !alarm && guard.fault == heater::Fault::None;
    return TestEvent::None;
  }
};
