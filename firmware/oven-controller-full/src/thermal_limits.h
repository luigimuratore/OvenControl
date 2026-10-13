#pragma once
#include <cmath>

namespace thermal {
constexpr float kMaxTarget = 400.0f;
// Keep the existing 15 °C margins above the maximum recipe target and cutoff.
constexpr float kHardLimit = kMaxTarget + 15.0f;
constexpr float kMaxReading = kHardLimit + 15.0f;

inline bool validTarget(float c) {
  return std::isfinite(c) && c >= 0 && c <= kMaxTarget;
}
inline bool validReading(float c, bool diagnostic) {
  return std::isfinite(c) && c >= (diagnostic ? -50.0f : -20.0f) && c <= kMaxReading;
}
inline bool belowHardLimit(float c) {
  return std::isfinite(c) && c < kHardLimit;
}
inline bool healthyPair(float first, bool firstValid, float second, bool secondValid) {
  // Probe difference is diagnostic only; each probe must still be valid and
  // below the absolute cutoff. Check both explicitly, including nonfinite data.
  return firstValid && secondValid && belowHardLimit(first) && belowHardLimit(second);
}
} // namespace thermal
