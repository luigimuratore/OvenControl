#pragma once
#include "manual_control.h"
#include "pause_control.h"

namespace regulation {
// Volatile checkpoint; never reconstructed from a report after a reboot.
struct FaultResume {
  bool available = false, manual = false, wasPaused = false;
  uint8_t step = 0;
  uint32_t faultAt = 0, segmentAt = 0, activeBefore = 0, holdMs = 0;
  float origin = NAN, reference = NAN;
  ProgramPause pause;
  ::manual::Session session;
  void clear() { *this = FaultResume{}; }
  void capture(const profile::Step &requested, uint8_t index, bool paused, const ProgramPause &savedPause,
               float startC, uint32_t segmentStarted, uint32_t elapsedBefore, uint32_t held, uint32_t now) {
    clear(); step = index; wasPaused = paused; pause = savedPause;
    origin = startC; segmentAt = segmentStarted; activeBefore = elapsedBefore; holdMs = held; faultAt = now;
    reference = !paused ? profile::setpoint(requested, startC, uint32_t(now-segmentStarted)) :
      requested.type == profile::Type::Hold ? requested.target : requested.type == profile::Type::Ramp ?
      fminf(requested.target, savedPause.targetC) : fmaxf(requested.target, savedPause.targetC);
    available = std::isfinite(reference) && thermal::belowHardLimit(reference) &&
      (!paused || (savedPause.active && thermal::validTarget(savedPause.targetC)));
  }
  void capture(const ::manual::Session &saved, uint32_t now) {
    clear(); manual = true; faultAt = now; session = saved; session.freeze(now);
    reference = session.reference;
    available = session.active() && std::isfinite(reference) && thermal::belowHardLimit(reference);
  }
  bool safePair(float first, bool firstValid, float second, bool secondValid,
                const thermal::ProtectionSettings &settings = thermal::ProtectionSettings{}) const {
    return available && thermal::healthyPair(first, firstValid, second, secondValid) &&
      !thermal::probeDeltaExceeded(first,second,settings) && !thermal::meanOvershootExceeded(first, second, reference, settings);
  }
  bool restore(const profile::Step &requested, uint32_t now, float &startC,
               uint32_t &segmentStarted, uint32_t &elapsedBefore, profile::HoldTimer &timer) const {
    if (!available || manual) return false;
    startC = origin; segmentStarted = segmentAt + uint32_t(now-faultAt); elapsedBefore = activeBefore;
    if (wasPaused) {
      auto saved = pause;
      if (!saved.resume(requested, now, startC, segmentStarted, elapsedBefore)) return false;
    }
    timer.reset(now); timer.elapsedMs = holdMs; // No in-band interval may bridge the alarm.
    return true;
  }
  bool restore(uint32_t now, ::manual::Session &restored) const {
    if (!available || !manual || !session.active()) return false;
    restored = session; restored.rebase(now); return true;
  }
};
} // namespace regulation
