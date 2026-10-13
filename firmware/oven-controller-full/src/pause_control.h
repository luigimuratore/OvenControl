#pragma once
#include <cmath>
#include <cstdint>
#include "profile.h"
#include "thermal_limits.h"

namespace regulation {
// Pauses program progress while PID regulates a captured, bounded measured mean.
// Segment time restarts on resume; accumulated active step time is preserved.
class ProgramPause {
public:
  bool active = false;
  float targetC = NAN;
  uint32_t frozenStepMs = 0;
  void clear() { active = false; targetC = NAN; frozenStepMs = 0; }
  bool begin(float measured, uint32_t now, uint32_t segmentStarted, uint32_t activeBefore) {
    if (active || !thermal::validReading(measured, false) || !thermal::belowHardLimit(measured)) return false;
    targetC = fmaxf(0, fminf(thermal::kMaxTarget, measured));
    frozenStepMs = activeBefore + uint32_t(now - segmentStarted);
    active = true; return true;
  }
  bool resume(const profile::Step &step, uint32_t now, float &segmentStartC,
              uint32_t &segmentStarted, uint32_t &activeBefore) {
    if (!active || !thermal::validTarget(targetC) || !thermal::validTarget(step.target) ||
        (step.type != profile::Type::Hold && (!std::isfinite(step.rate) || step.rate < .1f))) return false;
    segmentStartC = step.type == profile::Type::Ramp ? fminf(step.target, targetC) :
      step.type == profile::Type::Cooldown ? fmaxf(step.target, targetC) : step.target;
    segmentStarted = now; activeBefore = frozenStepMs;
    active = false; return true;
  }
};
} // namespace regulation
