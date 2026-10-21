#pragma once
#include <cstdint>
#include "thermal_limits.h"

namespace probes {
enum class RecoveryResult : uint8_t { NotAttempted, Confirmed, Failed, Pending };
enum class Confirmation : uint8_t { Safe, Invalid, Unsafe };
enum class RecoveryFailure : uint8_t { None, ConsecutiveFaults, Unstable, Unsafe };

// Faulted/out-of-range samples may be verified, never used to control heat.
// A fault-free overtemperature is a thermal alarm, not a transient to retry.
inline Confirmation confirmation(uint8_t fault, uint16_t raw, float c, bool diagnostic = false, bool rawFault = false) {
  if (fault || rawFault || raw == 0 || raw >= 32767 || !std::isfinite(c)) return Confirmation::Invalid;
  if (!thermal::belowHardLimit(c)) return Confirmation::Unsafe;
  return thermal::validReading(c, diagnostic) ? Confirmation::Safe : Confirmation::Invalid;
}

// The caller must perform complete new conversions; this class never enables heat.
class Recovery {
  uint8_t faults = 0, reads = 0;
 public:
  static constexpr uint8_t kMaxConsecutiveFaults = 10;
  // Bound alternating good/fault readings too: the main loop must not retry forever.
  static constexpr uint8_t kMaxConfirmationReads = kMaxConsecutiveFaults + 2;
  RecoveryFailure failure = RecoveryFailure::None;

  RecoveryResult begin(Confirmation initial, bool enabled = true) {
    failure = RecoveryFailure::None; faults = initial == Confirmation::Invalid ? 1 : 0; reads = 0;
    if (!enabled || initial == Confirmation::Safe) return RecoveryResult::NotAttempted;
    if (initial == Confirmation::Unsafe) { failure = RecoveryFailure::Unsafe; return RecoveryResult::Failed; }
    return RecoveryResult::Pending;
  }
  RecoveryResult confirm(Confirmation result) {
    if (failure != RecoveryFailure::None) return RecoveryResult::Failed;
    ++reads;
    if (result == Confirmation::Unsafe) failure = RecoveryFailure::Unsafe;
    else if (result == Confirmation::Safe) {
      faults = 0; return RecoveryResult::Confirmed;
    } else {
      if (++faults >= kMaxConsecutiveFaults) failure = RecoveryFailure::ConsecutiveFaults;
    }
    if (failure == RecoveryFailure::None && reads >= kMaxConfirmationReads) failure = RecoveryFailure::Unstable;
    return failure == RecoveryFailure::None ? RecoveryResult::Pending : RecoveryResult::Failed;
  }

  template <typename Inhibit, typename ReadSafe>
  RecoveryResult verify(Confirmation initial, bool enabled,
                        Inhibit inhibit, ReadSafe readSafe) {
    const auto started = begin(initial, enabled);
    if (started != RecoveryResult::Pending) return started;
    inhibit();
    for (uint8_t i = 0; i < kMaxConfirmationReads; ++i) {
      const auto result = confirm(readSafe());
      if (result != RecoveryResult::Pending) return result;
    }
    failure = RecoveryFailure::Unstable;
    return RecoveryResult::Failed;
  }
};

// Retry complete pairs, including the healthy sibling: one new pair with both
// samples safe confirms recovery. Each probe retains its own ten-error limit;
// a changing fault bit cannot reset that count. All attempts remain bounded.
class PairRecovery {
  bool started = false;
  RecoveryResult result = RecoveryResult::NotAttempted;
 public:
  Recovery probe[2];
  RecoveryResult observe(Confirmation first, Confirmation second) {
    if (started && result != RecoveryResult::Pending) return result;
    const auto a = started ? probe[0].confirm(first) : probe[0].begin(first);
    const auto b = started ? probe[1].confirm(second) : probe[1].begin(second);
    started = true;
    if (a == RecoveryResult::Failed || b == RecoveryResult::Failed) result = RecoveryResult::Failed;
    else if (a == RecoveryResult::NotAttempted && b == RecoveryResult::NotAttempted) result = RecoveryResult::NotAttempted;
    else if (a == RecoveryResult::Confirmed && b == RecoveryResult::Confirmed) result = RecoveryResult::Confirmed;
    else result = RecoveryResult::Pending;
    return result;
  }
};
} // namespace probes
