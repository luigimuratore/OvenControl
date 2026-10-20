#pragma once
#include <cmath>
#include <cstdint>

namespace thermal {
constexpr float kMaxTarget = 400.0f;
// Keep the existing 15 °C margins above the maximum recipe target and cutoff.
constexpr float kHardLimit = kMaxTarget + 15.0f;
constexpr float kMaxReading = kHardLimit + 15.0f;
constexpr float kMaxMeanOvershoot = 10.0f;
struct ProtectionSettings {
  uint32_t version = 1;
  float maxProbeDeltaC = 30.0f, maxMeanOvershootC = kMaxMeanOvershoot;
  bool probeDeltaEnabled = true, meanOvershootEnabled = true;
};
inline bool validSettings(const ProtectionSettings &s) {
  return s.version == 1 && std::isfinite(s.maxProbeDeltaC) && s.maxProbeDeltaC >= .1f && s.maxProbeDeltaC <= kMaxReading &&
    std::isfinite(s.maxMeanOvershootC) && s.maxMeanOvershootC >= .1f && s.maxMeanOvershootC <= kMaxReading;
}
inline bool probeDeltaExceeded(float first, float second, const ProtectionSettings &s) {
  return s.probeDeltaEnabled && std::isfinite(first) && std::isfinite(second) && fabsf(first-second) > s.maxProbeDeltaC;
}

inline bool meanOverTarget(float first, float second, float target, float limit = kMaxMeanOvershoot) {
  return std::isfinite(first) && std::isfinite(second) && std::isfinite(target) &&
    (first + second) * 0.5f > target + limit;
}
inline bool meanOvershootExceeded(float first, float second, float target, const ProtectionSettings &s) {
  return s.meanOvershootEnabled && meanOverTarget(first,second,target,s.maxMeanOvershootC);
}

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
