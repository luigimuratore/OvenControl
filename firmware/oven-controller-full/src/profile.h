#pragma once

#include <cmath>
#include <cstdint>

namespace profile {
enum class Type : uint8_t { Ramp, Hold, Cooldown };

struct Step {
  Type type;
  float target;
  float rate;       // °C/min for ramps and passive cooldowns
  uint16_t minutes; // Hold duration
};

constexpr float kBand = 1.0f;

inline bool inBand(float first, float second, float target) {
  return std::isfinite(first) && std::isfinite(second) && std::isfinite(target) &&
         fabsf(first - target) <= kBand && fabsf(second - target) <= kBand;
}

// Count only intervals bounded by two valid, in-band samples of this hold.
class HoldTimer {
 public:
  uint32_t elapsedMs = 0;
  void reset(uint32_t now) { elapsedMs = 0; previousAt = now; previousInBand = false; }
  void update(uint32_t now, bool currentInBand, bool running = true) {
    const uint32_t dt = uint32_t(now - previousAt);
    if (running && currentInBand && previousInBand && dt <= 3000) elapsedMs += dt;
    previousAt = now; previousInBand = running && currentInBand;
  }
 private:
  uint32_t previousAt = 0;
  bool previousInBand = false;
};

inline float setpoint(const Step &step, float startC, uint32_t elapsedMs) {
  if (step.type == Type::Hold) return step.target;
  float delta = step.rate * (elapsedMs / 60000.0f);
  return step.type == Type::Ramp ? fminf(step.target, startC + delta)
                                  : fmaxf(step.target, startC - delta);
}

inline bool rampDone(const Step &step, float startC, uint32_t elapsedMs, float actualC) {
  return setpoint(step, startC, elapsedMs) >= step.target - 0.001f &&
         actualC >= step.target - kBand;
}

inline bool cooldownDone(const Step &step, float startC, uint32_t elapsedMs, float hottestC) {
  return setpoint(step, startC, elapsedMs) <= step.target + 0.001f &&
         hottestC <= step.target + kBand;
}
} // namespace profile
