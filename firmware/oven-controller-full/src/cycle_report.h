#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "profile.h"

namespace reports {
constexpr uint32_t kVersion = 2; // Same storage layout; v2 hold band is ±5 °C, v1 was ±1 °C.
constexpr size_t kMaxSteps = 12;
enum class Outcome : uint8_t { None, Completed, Stopped, Fault };
struct Stats {
  uint32_t samples = 0, invalidPairs = 0, valid[2] = {}, trackingSamples = 0, saturatedSamples = 0;
  float start[2] = {NAN, NAN}, end[2] = {NAN, NAN};
  float low[2] = {NAN, NAN}, high[2] = {NAN, NAN};
  float maxDelta = NAN, maxOvershoot = 0, maxLag = 0;
  double absoluteErrorSum = 0, dutySum = 0;
  void observe(float first, float second, bool validFirst, bool validSecond,
               bool track, float reference, float duty, float limit) {
    ++samples;
    const float values[] = {first, second};
    const bool good[] = {validFirst && std::isfinite(first), validSecond && std::isfinite(second)};
    if (!good[0] || !good[1]) ++invalidPairs;
    for (size_t i = 0; i < 2; ++i) if (good[i]) {
      if (!valid[i]++) start[i] = low[i] = high[i] = values[i];
      end[i] = values[i]; low[i] = fminf(low[i], values[i]); high[i] = fmaxf(high[i], values[i]);
    }
    if (!good[0] || !good[1]) return;
    const float delta = fabsf(first - second);
    maxDelta = std::isfinite(maxDelta) ? fmaxf(maxDelta, delta) : delta;
    if (!track || !std::isfinite(reference) || !std::isfinite(duty)) return;
    ++trackingSamples;
    const float mean = (first + second) * 0.5f;
    absoluteErrorSum += fabsf(mean - reference); dutySum += duty;
    maxOvershoot = fmaxf(maxOvershoot, fmaxf(first, second) - reference);
    maxLag = fmaxf(maxLag, reference - mean);
    if (duty >= limit - 0.001f) ++saturatedSamples;
  }
};
struct StepResult {
  profile::Step requested{};
  bool started = false, completed = false;
  uint32_t activeMs = 0, pausedMs = 0, holdInBandMs = 0;
  Stats stats;
};
struct Snapshot {
  uint32_t version = kVersion, boot = 0, startMs = 0, endMs = 0;
  uint64_t startUtc = 0, endUtc = 0;
  uint32_t activeMs = 0, pausedMs = 0, pauseCount = 0;
  uint8_t stepCount = 0, completedSteps = 0;
  Outcome outcome = Outcome::None;
  char recipeId[33] = {}, recipeName[49] = {}, reason[160] = {};
  float kp = 0, ki = 0, kd = 0, maxPower = 0;
  uint32_t windowMs = 0;
  bool ssr = false;
  Stats stats;
  StepResult steps[kMaxSteps];
};
// Main-loop only. Time accounting is independent of sample rate and ring history.
class Recorder {
 public:
  Snapshot data;
  bool active = false, paused = false;
  uint8_t step = 0;
  void begin(uint32_t now, const profile::Step *steps, uint8_t count) {
    data = Snapshot{}; active = count > 0 && count <= kMaxSteps; paused = false; step = 0;
    data.startMs = previousAt = now; data.stepCount = active ? count : 0;
    for (size_t i = 0; i < data.stepCount; ++i) data.steps[i].requested = steps[i];
    if (active) data.steps[0].started = true;
  }
  void account(uint32_t now) {
    if (!active) return;
    const uint32_t dt = uint32_t(now - previousAt); previousAt = now;
    if (paused) { data.pausedMs += dt; if (step < data.stepCount) data.steps[step].pausedMs += dt; }
    else { data.activeMs += dt; if (step < data.stepCount) data.steps[step].activeMs += dt; }
  }
  void setPaused(uint32_t now, bool value) {
    if (!active || paused == value) return;
    account(now); if (value) ++data.pauseCount; paused = value;
  }
  void observe(uint32_t now, float first, float second, bool validFirst, bool validSecond,
               float reference, float duty) {
    if (!active || step >= data.stepCount) return;
    account(now);
    const bool tracking = !paused && data.steps[step].requested.type != profile::Type::Cooldown;
    data.stats.observe(first, second, validFirst, validSecond, tracking, reference, duty, data.maxPower);
    data.steps[step].stats.observe(first, second, validFirst, validSecond, tracking, reference, duty, data.maxPower);
  }
  void advance(uint32_t now, uint32_t holdInBandMs) {
    if (!active || step >= data.stepCount) return;
    account(now); data.steps[step].holdInBandMs = holdInBandMs;
    data.steps[step].completed = true; ++data.completedSteps;
    if (++step < data.stepCount) data.steps[step].started = true;
  }
  bool finish(uint32_t now, Outcome outcome, uint32_t holdInBandMs) {
    if (!active || outcome == Outcome::None) return false;
    account(now);
    if (step < data.stepCount) data.steps[step].holdInBandMs = holdInBandMs;
    data.endMs = now; data.outcome = outcome; active = false;
    return true;
  }
 private:
  uint32_t previousAt = 0;
};
} // namespace reports
