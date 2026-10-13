#pragma once
#include <cmath>
#include <cstdint>
#include "profile.h"

namespace regulation {
inline float clamp(float value, float low, float high) { return fmaxf(low, fminf(value, high)); }
inline bool continuousHold(const profile::Step &previous, const profile::Step &next) {
  return (previous.type == profile::Type::Ramp || previous.type == profile::Type::Hold) &&
    next.type == profile::Type::Hold && std::isfinite(previous.target) && std::isfinite(next.target) &&
    fabsf(previous.target - next.target) <= 0.1f;
}
inline bool continuousControl(const profile::Step &previous, const profile::Step &next) {
  if (!std::isfinite(previous.target) || !std::isfinite(next.target)) return false;
  return continuousHold(previous, next) ||
    (next.type == profile::Type::Cooldown && next.target < previous.target) ||
    (previous.type == profile::Type::Cooldown && next.type == profile::Type::Hold &&
      fabsf(previous.target - next.target) <= .1f);
}
class Pid {
  float previousMean = NAN;
  uint32_t previousAt = 0;
  bool sampled = false;
public:
  float integral = 0, derivative = 0, proportional = 0, output = 0;
  bool meanLimited = false;
  void reset() {
    integral = derivative = proportional = output = 0;
    previousMean = NAN; previousAt = 0; sampled = meanLimited = false;
  }
  float update(float target, float actual, float kp, float ki, float kd, float maxPower, uint32_t now,
               bool descending = false) {
    if (!std::isfinite(target) || !std::isfinite(actual) || !std::isfinite(kp) || !std::isfinite(ki) ||
        !std::isfinite(kd) || !std::isfinite(maxPower) || kp < 0 || ki < 0 || kd < 0 ||
        maxPower < 1 || maxPower > 100) { reset(); return 0; }
    const float dt = sampled ? clamp(float(uint32_t(now - previousAt)) / 1000.0f, 0.1f, 2.0f) : 0.5f;
    const float error = target - actual;
    derivative = 0.8f * derivative + 0.2f * (sampled ? -(actual - previousMean) / dt : 0);
    proportional = kp * error;
    integral = clamp(integral, 0, maxPower);
    const float candidate = clamp(integral + ki * error * dt, 0, maxPower);
    const float raw = proportional + candidate + kd * derivative;
    // Conditional integration prevents further windup at either output bound.
    if (!((raw >= maxPower && error > 0) || (raw <= 0 && error < 0))) integral = candidate;
    output = clamp(proportional + integral + kd * derivative, 0, maxPower);
    // This is a regulation override on the same measured mean as the PID.
    // Independent hottest-probe +8 C and absolute cutoffs remain in main.cpp.
    // Never erase the heat-loss compensation merely because heat is inhibited.
    // A descending reference may ask for heat only below the curve, never
    // above it, even if stored integral or derivative would request heat.
    meanLimited = actual >= target + (descending ? 0.0f : 1.0f);
    if (meanLimited) output = 0;
    previousMean = actual; previousAt = now; sampled = true;
    return output;
  }
};
} // namespace regulation
