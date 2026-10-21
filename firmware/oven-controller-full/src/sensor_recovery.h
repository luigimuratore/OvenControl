#pragma once
#include <cstdint>

namespace probes {
enum class RecoveryResult : uint8_t { NotAttempted, Confirmed, Failed, Pending };
enum class Confirmation : uint8_t { Safe, OvUv, Unsafe };
enum class RecoveryFailure : uint8_t { None, ConsecutiveFaults, Unstable, Unsafe };

// Only isolated OV/UV faults may be verified again. The caller must perform
// complete new conversions and validate each one; this class never enables heat.
class Recovery {
  uint8_t faults = 0, good = 0, reads = 0;
 public:
  static constexpr uint8_t kOvUv = 0x04;
  static constexpr uint8_t kMaxConsecutiveFaults = 10;
  // Bound alternating good/fault readings too: the main loop must not retry forever.
  static constexpr uint8_t kMaxConfirmationReads = kMaxConsecutiveFaults + 2;
  RecoveryFailure failure = RecoveryFailure::None;

  RecoveryResult begin(uint8_t fault, bool enabled = true) {
    failure = RecoveryFailure::None; faults = 1; good = reads = 0;
    return enabled && fault == kOvUv ? RecoveryResult::Pending : RecoveryResult::NotAttempted;
  }
  RecoveryResult confirm(Confirmation result) {
    ++reads;
    if (result == Confirmation::Unsafe) failure = RecoveryFailure::Unsafe;
    else if (result == Confirmation::Safe) {
      faults = 0; if (++good == 2) return RecoveryResult::Confirmed;
    } else {
      good = 0; if (++faults >= kMaxConsecutiveFaults) failure = RecoveryFailure::ConsecutiveFaults;
    }
    if (failure == RecoveryFailure::None && reads >= kMaxConfirmationReads) failure = RecoveryFailure::Unstable;
    return failure == RecoveryFailure::None ? RecoveryResult::Pending : RecoveryResult::Failed;
  }

  template <typename Inhibit, typename ReadSafe>
  RecoveryResult verify(uint8_t fault, bool enabled,
                        Inhibit inhibit, ReadSafe readSafe) {
    if (begin(fault, enabled) != RecoveryResult::Pending) return RecoveryResult::NotAttempted;
    inhibit();
    for (uint8_t i = 0; i < kMaxConfirmationReads; ++i) {
      const auto result = confirm(readSafe());
      if (result != RecoveryResult::Pending) return result;
    }
    failure = RecoveryFailure::Unstable;
    return RecoveryResult::Failed;
  }
};
} // namespace probes
