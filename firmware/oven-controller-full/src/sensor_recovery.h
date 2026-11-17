#pragma once
#include <cstdint>
#include "thermal_limits.h"

namespace probes {
constexpr uint32_t kDefaultBothFaultTimeoutSec = 20;
constexpr uint32_t kMinTimeoutSec = 1, kMaxTimeoutSec = 300;
struct RecoverySettings {
  uint32_t version = 2, bothFaultTimeoutSec = kDefaultBothFaultTimeoutSec;
};
static_assert(sizeof(RecoverySettings) == 8, "Stable probe availability settings layout");
inline bool validSettings(const RecoverySettings &s) {
  return s.version == 2 && s.bothFaultTimeoutSec >= kMinTimeoutSec && s.bothFaultTimeoutSec <= kMaxTimeoutSec;
}
enum class Confirmation : uint8_t { Safe, Invalid, Unsafe };
inline Confirmation confirmation(uint8_t fault, uint16_t raw, float c, bool diagnostic = false, bool rawFault = false) {
  if (fault || rawFault || raw == 0 || raw >= 32767 || !std::isfinite(c)) return Confirmation::Invalid;
  if (!thermal::belowHardLimit(c)) return Confirmation::Unsafe;
  return thermal::validReading(c, diagnostic) ? Confirmation::Safe : Confirmation::Invalid;
}
// No retries or cached good readings: a new completed pair either supplies at
// least one usable sample or keeps all outputs inhibited. Time wraps safely.
class Availability {
  bool missing = false;
  uint32_t since = 0;
 public:
  void observe(uint32_t now, bool anyValid) {
    if (anyValid) missing = false;
    else if (!missing) { missing = true; since = now; }
  }
  bool waiting() const { return missing; }
  uint32_t elapsedMs(uint32_t now) const { return missing ? uint32_t(now - since) : 0; }
  bool expired(uint32_t now, uint32_t seconds) const { return missing && elapsedMs(now) >= seconds * 1000UL; }
};
} // namespace probes
