#pragma once
#include <cstdint>
#include <cmath>
#include "thermal_limits.h"

namespace manual {
enum class Mode : uint8_t { Idle, Regulating, Cooldown };
class Session {
  uint32_t lastAt = 0;
  uint64_t duration = 0;
  double rampReference = NAN;
  void account(uint32_t now) {
    if (active()) {
      const uint32_t elapsed = uint32_t(now - lastAt);
      duration += elapsed;
      const double movement = rateCMin * (elapsed / 60000.0);
      rampReference = rampReference < target ? fmin(target, rampReference + movement) : fmax(target, rampReference - movement);
      reference = float(rampReference);
    }
    lastAt = now;
  }
public:
  Mode mode = Mode::Idle;
  float target = NAN;
  float reference = NAN, rateCMin = 1;
  bool coolingToTarget = false;
  bool cooldownComplete = false;
  uint32_t startedAt = 0;
  bool active() const { return mode != Mode::Idle; }
  bool descending() const { return mode == Mode::Cooldown || coolingToTarget; }
  bool heatingAllowed() const { return active() && !(mode == Mode::Cooldown && cooldownComplete); }
  uint64_t elapsedMs(uint32_t now) const { return duration + (active() ? uint32_t(now - lastAt) : 0); }
  static bool validRate(float rate) { return std::isfinite(rate) && rate >= .1f; }
  bool start(float requested, float rate, float hottest, float actual, uint32_t now) {
    if (active() || !thermal::validTarget(requested) || !validRate(rate) ||
        !thermal::belowHardLimit(hottest) || !std::isfinite(actual)) return false;
    target = requested; rateCMin = rate; reference = actual; rampReference = actual; startedAt = lastAt = now; duration = 0;
    mode = Mode::Regulating; coolingToTarget = hottest > requested + 1; cooldownComplete = false;
    return true;
  }
  bool changeTarget(float requested, float rate, float hottest, float actual, uint32_t now) {
    if (!active() || !thermal::validTarget(requested) || !validRate(rate) ||
        !thermal::belowHardLimit(hottest) || !std::isfinite(actual)) return false;
    const bool wasCooling = mode == Mode::Cooldown || coolingToTarget;
    account(now);
    if (wasCooling) { reference = actual; rampReference = actual; } // Restart from the real temperature.
    target = requested; rateCMin = rate; mode = Mode::Regulating;
    cooldownComplete = false;
    coolingToTarget = hottest > requested + 1 || reference > requested + 1;
    return true;
  }
  void tick(float hottest, uint32_t now) {
    account(now);
    if (mode == Mode::Regulating && coolingToTarget && std::isfinite(hottest) && hottest <= target + 1 && reference == target)
      coolingToTarget = false;
    if (mode == Mode::Cooldown && reference == target && std::isfinite(hottest) && hottest <= target + 1)
      cooldownComplete = true; // Monitor only after reaching the endpoint; no automatic reheating.
  }
  bool cooldown(float requested, float rate, uint32_t now) {
    if (!active() || !thermal::validTarget(requested) || !validRate(rate)) return false;
    account(now);
    if (requested > reference) return false;
    target = requested; rateCMin = rate; mode = Mode::Cooldown; coolingToTarget = false; cooldownComplete = false;
    return true;
  }
  void stop(uint32_t now) { account(now); mode = Mode::Idle; coolingToTarget = false; }
};
inline const char *modeName(Mode mode) {
  return mode == Mode::Regulating ? "regulating" : mode == Mode::Cooldown ? "cooldown" : "idle";
}
} // namespace manual
