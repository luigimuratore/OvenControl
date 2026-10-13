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
constexpr float kHoldBand = 5.0f;

// Continue an existing reference into cooldown instead of following probe overshoot.
// A cycle starting with cooldown has no previous reference and uses the measured mean.
inline float startTemperature(const Step &step, float measuredC, float previousSetpoint = NAN) {
  return step.type == Type::Cooldown && std::isfinite(previousSetpoint) ? previousSetpoint : measuredC;
}

inline bool inBand(float first, float second, float target) {
  return std::isfinite(first) && std::isfinite(second) && std::isfinite(target) &&
         fabsf(first - target) <= kHoldBand && fabsf(second - target) <= kHoldBand;
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

inline bool rampToHold(const Step &step, const Step *next) {
  return step.type == Type::Ramp && next && next->type == Type::Hold &&
         std::isfinite(step.target) && std::isfinite(next->target) && fabsf(step.target - next->target) <= 0.1f;
}
inline bool rampDone(const Step &step, float startC, uint32_t elapsedMs, float coldestC,
                     float hottestC = NAN, const Step *next = nullptr) {
  const float reference = setpoint(step, startC, elapsedMs);
  if (step.type != Type::Ramp || !std::isfinite(startC) || !std::isfinite(step.rate) || step.rate <= 0 ||
      !std::isfinite(reference) || reference < step.target - 0.001f) return false;
  // A following hold must be reachable using the very same qualifying band.
  if (rampToHold(step, next)) return inBand(coldestC, hottestC, next->target);
  return std::isfinite(coldestC) && coldestC >= step.target - kBand;
}

inline bool cooldownDone(const Step &step, float startC, uint32_t elapsedMs, float hottestC) {
  return setpoint(step, startC, elapsedMs) <= step.target + 0.001f &&
         hottestC <= step.target + kBand;
}

// Hold reports qualifying time still needed. Ramp/cooldown are estimates at the recipe rate.
inline float remainingSec(const Step &step, float startC, uint32_t elapsedMs,
                          uint32_t holdMs, float coldestC, float hottestC, const Step *next = nullptr) {
  if (step.type == Type::Hold) {
    const uint32_t required = uint32_t(step.minutes) * 60000UL;
    return holdMs >= required ? 0 : std::ceil((required - holdMs) / 1000.0f);
  }
  if (!std::isfinite(startC) || !std::isfinite(step.rate) || step.rate <= 0 ||
      !std::isfinite(coldestC) || !std::isfinite(hottestC)) return NAN;
  const bool ramp = step.type == Type::Ramp;
  const float span = ramp ? step.target - startC : startC - step.target;
  const float referenceSeconds = fmaxf(0, span * 60 / step.rate - elapsedMs / 1000.0f);
  const float distance = rampToHold(step, next) ?
    fmaxf(next->target - kHoldBand - coldestC, hottestC - next->target - kHoldBand) :
    ramp ? step.target - kBand - coldestC : hottestC - step.target - kBand;
  return std::ceil(fmaxf(referenceSeconds, fmaxf(0, distance) * 60 / step.rate));
}
} // namespace profile
