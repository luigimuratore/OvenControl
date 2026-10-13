#include <cassert>
#include <iostream>
#include <type_traits>
#include "../src/cycle_report.h"
static_assert(std::is_trivially_copyable<reports::Snapshot>::value, "Report must support binary persistence");
int main() {
  profile::Step steps[] = {{profile::Type::Ramp, 50, 1, 0}, {profile::Type::Hold, 50, 0, 1}, {profile::Type::Cooldown, 30, 1, 0}};
  reports::Recorder r; r.begin(1000, steps, 3); r.data.maxPower = 30;
  r.observe(1000, 25, 27, true, true, 30, 30);
  r.observe(2000, 31, 33, true, true, 30, 10);
  r.setPaused(2500, true);
  r.observe(3000, 100, 100, true, true, 30, 0); // pause thermals count, tracking does not
  r.setPaused(4000, false);
  r.advance(5000, 0);
  r.observe(6000, 49.5, 50.5, true, true, 50, 20);
  r.observe(6500, 49.6, NAN, true, false, 50, 20);
  r.advance(7000, 500);
  r.observe(8000, 40, 41, true, true, 30, 0); // cooldown excluded from PID metrics
  r.advance(9000, 0);
  assert(r.finish(9000, reports::Outcome::Completed, 0));
  const auto &d = r.data;
  assert(d.activeMs == 6500 && d.pausedMs == 1500 && d.pauseCount == 1);
  assert(d.completedSteps == 3 && d.steps[0].activeMs == 2500 && d.steps[0].pausedMs == 1500);
  assert(d.steps[1].activeMs == 2000 && d.steps[1].holdInBandMs == 500);
  assert(d.stats.samples == 6 && d.stats.invalidPairs == 1 && d.stats.trackingSamples == 3);
  assert(d.stats.valid[0] == 6 && d.stats.valid[1] == 5);
  assert(d.stats.maxOvershoot == 2 && d.stats.maxLag == 4);
  assert(d.steps[0].stats.maxOvershoot == 2 && d.steps[1].stats.maxOvershoot == 0);
  assert(d.stats.maxDelta == 2 && d.stats.saturatedSamples == 1);
  assert(d.stats.absoluteErrorSum == 6 && d.stats.dutySum == 60);
  assert(d.stats.high[0] == 100 && d.steps[2].stats.trackingSamples == 0);
  assert(!r.finish(10000, reports::Outcome::Stopped, 0));
  r.observe(11000, 200, 200, true, true, 50, 100); assert(d.stats.samples == 6);
  // STOP during pause must preserve incomplete hold and exclude pause from active time.
  r.begin(100, steps + 1, 1); r.data.maxPower = 30;
  r.observe(100, NAN, NAN, false, false, 50, 0);
  r.setPaused(1100, true);
  assert(r.finish(3100, reports::Outcome::Stopped, 750));
  assert(r.data.activeMs == 1000 && r.data.pausedMs == 2000 && !r.data.steps[0].completed);
  assert(r.data.steps[0].holdInBandMs == 750 && r.data.stats.trackingSamples == 0);
  assert(!std::isfinite(r.data.stats.start[0]));
  // Timer wrap and fault sample included before finalization.
  r.begin(UINT32_MAX - 99, steps, 3); r.data.maxPower = 30;
  r.observe(0, 206, 200, true, true, 190, 30);
  assert(r.finish(100, reports::Outcome::Fault, 0));
  assert(r.data.activeMs == 200 && r.data.stats.maxOvershoot == 13 && r.data.completedSteps == 0);
  assert(!r.data.steps[1].started);
  reports::Recorder reboot; assert(!reboot.active && reboot.data.outcome == reports::Outcome::None);
  assert(reports::kVersion == 3);
  assert(!reports::supportedVersion(0) && !reports::supportedVersion(4));
  for (unsigned version = 1; version <= 3; ++version) assert(reports::supportedVersion(version));
  // A single hot probe must not inflate the overshoot of the regulated mean.
  reports::Stats asymmetric;
  asymmetric.observe(179.22f, 170.78f, true, true, true, 175, 20, 60);
  assert(asymmetric.maxOvershoot == 0);
  asymmetric.observe(179.22f, 174.78f, true, true, true, 175, 20, 60);
  assert(asymmetric.maxOvershoot == 2);
  asymmetric.observe(190, NAN, true, false, true, 175, 20, 60);
  assert(asymmetric.maxOvershoot == 2 && asymmetric.invalidPairs == 1);
  std::cout << "OK: cycle/step times, pauses, hold, invalid probes, PID metrics, cooldown exclusion, STOP/fault, wrap, reset\n";
}
