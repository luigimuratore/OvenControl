#pragma once
#include <cstdint>

namespace probes {
enum class RecoveryResult : uint8_t { NotAttempted, Confirmed, Failed };

// Only isolated OV/UV faults may be verified again. The caller must perform
// complete new conversions and validate each one; this class never enables heat.
class Recovery {
 public:
  static constexpr uint8_t kOvUv = 0x04;
  static constexpr uint32_t kQuietMs = 60000;

  template <typename Inhibit, typename ReadSafe>
  RecoveryResult verify(uint8_t fault, uint32_t now, bool enabled,
                        Inhibit inhibit, ReadSafe readSafe) {
    if (!enabled || fault != kOvUv ||
        (attempted && uint32_t(now - lastAttemptAt) < kQuietMs))
      return RecoveryResult::NotAttempted;
    attempted = true;
    lastAttemptAt = now;
    inhibit();
    // Stop on the first failed confirmation: no unbounded retry loop.
    if (!readSafe() || !readSafe()) return RecoveryResult::Failed;
    return RecoveryResult::Confirmed;
  }

 private:
  bool attempted = false;
  uint32_t lastAttemptAt = 0;
};
} // namespace probes
