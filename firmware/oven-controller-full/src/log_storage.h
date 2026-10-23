#pragma once

#include <cstddef>
#include <cstdint>

namespace logs {
// NVS needs a spare page for compaction. Keep another page for settings and
// safety state; log retention is best effort when other firmware shares NVS.
constexpr size_t kReserveEntries = 2 * 126;

inline size_t stringEntries(size_t length) { return 1 + (length + 1 + 31) / 32; }

enum class WriteStatus : uint8_t { Saved, NoSpace, Error };
struct WriteResult { bool saved = false, cleanupFailed = false; size_t reclaimed = 0; };
// Only a real out-of-space write triggers reclamation. Aggregate free-entry
// counts cannot describe page fragmentation. Never erase settings or namespaces.
template <class Store, class Write>
WriteResult writeWithRecovery(Store &store, uint32_t newest, size_t capacity, bool allowCleanup, Write write) {
  WriteResult result;
  auto status = write();
  if (status == WriteStatus::Saved) { result.saved = true; return result; }
  if (status != WriteStatus::NoSpace || !allowCleanup || !capacity) return result;
  const size_t oldest = uint32_t(newest + 1) % capacity;
  for (size_t i = 0; i < capacity; ++i) {
    const size_t slot = (oldest + i) % capacity;
    if (!store.has(slot)) continue;
    if (!store.remove(slot)) { result.cleanupFailed = true; return result; }
    ++result.reclaimed;
    status = write();
    if (status == WriteStatus::Saved) { result.saved = true; break; }
    if (status != WriteStatus::NoSpace) break;
  }
  return result;
}

template <class Store>
bool makeRoom(Store &store, uint32_t newest, size_t capacity, size_t required) {
  for (size_t i = 0; i <= capacity; ++i) {
    size_t free = 0;
    if (!store.freeEntries(free)) return false;
    if (free >= required) return true;
    if (i == capacity) return false;
    // The next slot is the oldest in a full ring. Missing slots are harmless,
    // including those left by a failed write in an older firmware.
    size_t slot = uint32_t(newest + 1 + uint32_t(i)) % capacity;
    if (store.has(slot) && !store.remove(slot)) return false;
  }
  return false;
}
} // namespace logs
