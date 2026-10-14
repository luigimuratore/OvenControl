#pragma once
#include <cstdint>

namespace probes {
enum class RecoveryResult : uint8_t { NotAttempted, Confirmed, Failed };
enum class Confirmation : uint8_t { Safe, OvUv, Unsafe };
enum class RecoveryFailure : uint8_t { None, ConsecutiveFaults, Unstable, Unsafe };

// Only isolated OV/UV faults may be verified again. The caller must perform
// complete new conversions and validate each one; this class never enables heat.
class Recovery {
 public:
  static constexpr uint8_t kOvUv = 0x04;
  static constexpr uint8_t kMaxConsecutiveFaults = 10;
  // Bound alternating good/fault readings too: the main loop must not retry forever.
  static constexpr uint8_t kMaxConfirmationReads = kMaxConsecutiveFaults + 2;
  RecoveryFailure failure = RecoveryFailure::None;

  template <typename Inhibit, typename ReadSafe>
  RecoveryResult verify(uint8_t fault, bool enabled,
                        Inhibit inhibit, ReadSafe readSafe) {
    failure = RecoveryFailure::None;
    if (!enabled || fault != kOvUv) return RecoveryResult::NotAttempted;
    inhibit();
    uint8_t faults = 1, good = 0; // Include the original 0x04 in the consecutive count.
    for (uint8_t i = 0; i < kMaxConfirmationReads; ++i) {
      const auto result = readSafe();
      if (result == Confirmation::Unsafe) { failure = RecoveryFailure::Unsafe; return RecoveryResult::Failed; }
      if (result == Confirmation::Safe) {
        faults = 0;
        if (++good == 2) return RecoveryResult::Confirmed;
      } else {
        good = 0;
        if (++faults >= kMaxConsecutiveFaults) { failure = RecoveryFailure::ConsecutiveFaults; return RecoveryResult::Failed; }
      }
    }
    failure = RecoveryFailure::Unstable;
    return RecoveryResult::Failed;
  }
};
} // namespace probes
