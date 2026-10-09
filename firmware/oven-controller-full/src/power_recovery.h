#pragma once
#include <cstdint>
#include <cstdio>

namespace recovery {
constexpr uint32_t kVersion = 1;
enum class Cause : uint8_t { Other, PowerOn, Brownout };
inline bool powerRelated(Cause cause) { return cause == Cause::PowerOn || cause == Cause::Brownout; }
struct Record {
  uint32_t version = kVersion, resetReason = 0;
  uint64_t checkpointUtc = 0, rebootUtc = 0;
  bool active = false, pending = false;
  char program[49]{};
};
struct Estimate { bool known = false; uint64_t upperSec = 0; };
inline Estimate estimate(uint64_t checkpoint, uint64_t reboot) {
  if (!checkpoint || !reboot || reboot < checkpoint) return {};
  Estimate result; result.known = true; result.upperSec = reboot - checkpoint;
  return result;
}
// Store::load returns 1 for valid bytes, 0 for absent, -1 for invalid/unreadable.
// Store::save is one committed NVS blob; failed writes never authorize restart.
class Journal {
public:
  Record data;
  bool present = false;
  template <class Store> bool load(Store &store) {
    Record candidate; const int result = store.load(candidate);
    if (result < 0 || (result > 0 && candidate.version != kVersion)) {
      data.pending = true; return false;
    }
    if (result > 0) { data = candidate; data.program[48] = 0; present = true; }
    return true;
  }
  template <class Store> bool boot(Store &store, bool legacyActive, uint64_t legacyCheckpoint, uint32_t reason) {
    if (!data.active && !data.pending && !legacyActive) return true;
    if (!data.pending) {
      if (!present) data.checkpointUtc = legacyCheckpoint;
      data.rebootUtc = 0; data.resetReason = reason;
    }
    data.active = false; data.pending = true;
    // Preserve the latch in RAM even if the durable write fails.
    const bool saved = store.save(data); if (saved) present = true; return saved;
  }
  template <class Store> bool begin(Store &store, const char *program, uint64_t utc) {
    if (data.pending || data.active) return false;
    Record candidate; candidate.active = true; candidate.checkpointUtc = utc;
    snprintf(candidate.program, sizeof(candidate.program), "%s", program);
    if (!store.save(candidate)) return false;
    data = candidate; present = true; return true;
  }
  template <class Store> bool checkpoint(Store &store, uint64_t utc) {
    if (!data.active || !utc || (data.checkpointUtc && utc < data.checkpointUtc)) return false;
    Record candidate = data; candidate.checkpointUtc = utc;
    if (!store.save(candidate)) return false;
    data = candidate; return true;
  }
  template <class Store> bool returned(Store &store, uint64_t bootUtc) {
    if (!data.pending || data.rebootUtc || !bootUtc || (data.checkpointUtc && bootUtc < data.checkpointUtc)) return false;
    Record candidate = data; candidate.rebootUtc = bootUtc;
    if (!store.save(candidate)) return false;
    data = candidate; return true;
  }
  template <class Store> bool finish(Store &store) {
    Record candidate = data; candidate.active = candidate.pending = false;
    if (!store.save(candidate)) { data.active = false; data.pending = true; return false; }
    data = candidate; return true;
  }
  template <class Store> bool acknowledge(Store &store) {
    Record candidate = data; candidate.active = candidate.pending = false;
    if (!store.save(candidate)) return false;
    data = candidate; return true;
  }
};
} // namespace recovery
