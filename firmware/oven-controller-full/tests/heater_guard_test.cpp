#include "../src/heater_guard.h"
#include "../src/profile.h"
#include <cassert>
#include <limits>
#include <cstdio>

int main() {
  assert(heater::validSettings(false, 30, 60000));
  assert(!heater::validSettings(false, 30, 59999));
  assert(heater::validSettings(true, 100, 1000));
  assert(!heater::validSettings(true, 101, 1000));
  assert(!heater::validSettings(true, 0, 1000));
  assert(!heater::validSettings(true, 30, 300001));
  heater::Guard guard;
  assert(!guard.tick(0));
  heater::Plan p;
  p.cycleActive = p.heatingAllowed = p.healthy = true;
  p.sampleAt = p.cycleAt = p.windowAt = 100; p.windowMs = 10000; p.duty = 25;
  guard.publish(p);
  assert(guard.tick(100)); assert(guard.tick(2599)); assert(!guard.tick(2600));
  p.sampleAt = 10000; guard.publish(p); assert(!guard.tick(10099)); assert(guard.tick(10100));
  p.heatingAllowed = false; guard.publish(p); assert(!guard.tick(10101)); // Pause/cooldown.
  guard.clear(); p.heatingAllowed = true; p.sampleAt = 100; guard.publish(p);
  assert(!guard.tick(3100) && guard.fault == heater::Fault::StaleControl);
  p.sampleAt = 3200; guard.publish(p); assert(!guard.tick(3200)); // No automatic recovery.
  guard.clear(); guard.publish(p); assert(guard.tick(3200) == false); // Window is in OFF interval.
  p.windowAt = 3200; guard.publish(p); assert(guard.tick(3200));
  guard.inhibit(); assert(!guard.tick(3201));
  assert(!guard.tick(200000) && guard.fault == heater::Fault::None); // STOP does not leave an active lease.
  guard.clear(); p.heatingAllowed = true; p.sampleAt = 43200100; guard.publish(p);
  assert(!guard.tick(43200100) && guard.fault == heater::Fault::CycleTimeout);
  guard.clear(); p.sampleAt = p.cycleAt = p.windowAt = 0; p.duty = std::numeric_limits<float>::quiet_NaN(); guard.publish(p);
  assert(!guard.tick(0) && guard.fault == heater::Fault::InvalidPlan);
  guard.clear(); p.duty = 100; p.healthy = false; guard.publish(p);
  assert(!guard.tick(0) && guard.fault == heater::Fault::InvalidPlan);
  guard.clear(); p.healthy = true; p.windowMs = 0; guard.publish(p);
  assert(!guard.tick(0) && guard.fault == heater::Fault::InvalidPlan);
  guard.clear(); p.windowMs = 10000; p.sampleAt = p.cycleAt = p.windowAt = UINT32_MAX - 500;
  guard.publish(p); assert(guard.tick(200));
  assert(!guard.tick(uint32_t(p.sampleAt + 3000)) && guard.fault == heater::Fault::StaleControl);

  profile::HoldTimer hold;
  hold.reset(0); hold.update(500, true); assert(hold.elapsedMs == 0);
  hold.update(1000, true); assert(hold.elapsedMs == 500);
  hold.update(1500, false); hold.update(2000, true); assert(hold.elapsedMs == 500);
  hold.update(2500, true); assert(hold.elapsedMs == 1000);
  hold.update(3000, true, false); hold.update(9000, true, false);
  hold.update(9500, true); assert(hold.elapsedMs == 1000);
  hold.update(10000, true); assert(hold.elapsedMs == 1500);
  hold.update(20000, true); assert(hold.elapsedMs == 1500); // Stale interval excluded.
  hold.reset(UINT32_MAX - 250); hold.update(UINT32_MAX - 50, true); hold.update(449, true);
  assert(hold.elapsedMs == 500);
  assert(profile::inBand(49, 51, 50)); assert(!profile::inBand(48.9, 50, 50));
  assert(!profile::inBand(50, std::numeric_limits<float>::quiet_NaN(), 50));
  puts("OK: PWM, pause/cooldown/STOP, stale control latch, cycle timeout, invalid plans, hold accounting, timer wrap");
}
