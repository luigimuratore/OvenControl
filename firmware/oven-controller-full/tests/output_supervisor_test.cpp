#include <cassert>
#include <iostream>
#include "../src/output_supervisor.h"

heater::Plan heating(uint32_t now) {
  heater::Plan p; p.cycleActive = p.heatingAllowed = p.healthy = true;
  p.sampleAt = p.cycleAt = p.windowAt = now; p.windowMs = 1000; p.duty = 100;
  return p;
}
int main() {
  OutputSupervisor s;
  assert(s.publish(heating(100)));
  s.tick(100, true, false); assert(s.relay && s.red && s.green);
  assert(!s.startTest(TestMode::RelayPulse, 100, 1000, true));
  assert(!s.enterTest(true));
  s.stopAll(); s.tick(110, true, false); assert(!s.relay);
  assert(s.startTest(TestMode::LedRed, 200, 1000, false));
  assert(!s.publish(heating(200)));
  s.tick(200, true, false); assert(!s.relay && s.red && !s.green);
  assert(!s.startTest(TestMode::RelayPulse, 201, 1000, false));
  s.stopTest(); assert(!s.red && !s.relay);
  // Diagnostic tests ignore probe/recipe alarms, but never clear their latch.
  s.guard.fault = heater::Fault::StaleControl;
  assert(s.startTest(TestMode::RelayPulse, 300, 600000, false));
  s.tick(300, true, true); assert(s.relay && s.red);
  assert(s.tick(2800, true, true) == TestEvent::ConnectionLost);
  assert(!s.relay && !s.red && s.diagnostic);
  assert(s.guard.fault == heater::Fault::StaleControl);
  s.stopTest(true); s.tick(2801, true, true); assert(!s.relay && !s.green);
  s.guard.clear(); assert(s.publish(heating(3000)));
  s.tick(3000, true, false); assert(s.relay);
  s.beginUpdate(); assert(!s.relay && !s.red && !s.green);
  assert(!s.publish(heating(3000)));
  assert(!s.enterTest(false));
  assert(!s.startTest(TestMode::RelayPulse, 3000, 1000, false));
  s.tick(3001, true, false); assert(!s.relay && !s.green);
  s.updating = false; s.tick(3002, true, false); assert(!s.relay);
  assert(s.startTest(TestMode::RelayPulse, 4000, 1000, false));
  s.tick(4000, true, false); assert(s.relay);
  s.beginUpdate(); s.tick(4001, true, false); assert(!s.relay && !s.green);
  OutputSupervisor reboot; reboot.tick(0, true, false); assert(!reboot.relay && !reboot.diagnostic);
  OutputSupervisor emergency;
  assert(emergency.publish(heating(5000)));
  emergency.tick(5000, true, false); assert(emergency.relay);
  emergency.emergencyStop(); assert(!emergency.relay && !emergency.red && !emergency.green);
  assert(emergency.emergency && !emergency.publish(heating(5001)));
  assert(!emergency.enterTest(false));
  assert(!emergency.startTest(TestMode::RelayPulse, 5001, 1000, false));
  emergency.stopAll(); emergency.tick(5002, true, false); assert(!emergency.relay && emergency.emergency);
  emergency.acknowledgeEmergency(); emergency.tick(5003, true, false); assert(!emergency.relay);
  assert(emergency.startTest(TestMode::RelayPulse, 6000, 1000, false));
  emergency.tick(6000, true, false); assert(emergency.relay);
  emergency.emergencyStop(); emergency.tick(6001, true, false);
  assert(!emergency.relay && !emergency.diagnostic && emergency.test.mode == TestMode::Idle);
  emergency.beginUpdate(); emergency.updating = false;
  emergency.tick(6002, true, false); assert(emergency.emergency && !emergency.relay);
  std::cout << "Output supervisor tests passed\n";
}
