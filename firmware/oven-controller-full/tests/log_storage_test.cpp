#include "../src/log_storage.h"
#include <cassert>
#include <cstdio>
#include <map>

struct Store {
  size_t total = 630, settingsEntries = 0;
  bool statsAvailable = true, removalAllowed = true;
  std::map<size_t, size_t> slots;
  size_t firstRemoved = 40, removals = 0;
  bool freeEntries(size_t &free) {
    if (!statsAvailable) return false;
    size_t used = settingsEntries;
    for (const auto &slot : slots) used += slot.second;
    free = total - used; return true;
  }
  bool has(size_t slot) { return slots.count(slot) != 0; }
  bool remove(size_t slot) {
    if (!removalAllowed) return false;
    if (!removals++) firstRemoved = slot;
    return slots.erase(slot) == 1;
  }
};

int main() {
  // Shared NVS almost full after using multiple firmware variants. Reclaim
  // only as many oldest log slots as necessary, preserving settings.
  Store full;
  full.settingsEntries = 340;
  for (size_t i = 0; i < 40; ++i) full.slots[i] = 6;
  assert(logs::makeRoom(full, 75, 40, logs::kReserveEntries));
  assert(full.firstRemoved == 36 && full.removals == 34);
  assert(full.settingsEntries == 340 && full.slots.size() == 6);
  assert(full.has(30) && full.has(35) && !full.has(36));

  // A healthy store keeps every event, including across repeated calls.
  Store healthy;
  healthy.slots[1] = 6;
  assert(logs::makeRoom(healthy, 1, 40, logs::kReserveEntries));
  assert(logs::makeRoom(healthy, 1, 40, logs::kReserveEntries));
  assert(healthy.removals == 0);

  // Missing slots from failed writes must not stop reclamation. Even when
  // other namespaces consume too much space, do not delete their contents.
  Store sparse;
  sparse.settingsEntries = 600; sparse.slots[1] = 6; sparse.slots[7] = 6;
  assert(!logs::makeRoom(sparse, 7, 40, logs::kReserveEntries));
  assert(sparse.removals == 2 && sparse.settingsEntries == 600);

  Store unavailable = healthy;
  unavailable.statsAvailable = false;
  assert(!logs::makeRoom(unavailable, 1, 40, logs::kReserveEntries));
  assert(unavailable.removals == 0 && unavailable.has(1));

  Store blocked;
  blocked.settingsEntries = 600; blocked.slots[1] = 6;
  blocked.removalAllowed = false;
  assert(!logs::makeRoom(blocked, 1, 40, logs::kReserveEntries));
  assert(blocked.has(1));

  Store rollover;
  rollover.settingsEntries = 370;
  rollover.slots[0] = 10;
  assert(logs::makeRoom(rollover, UINT32_MAX, 40, logs::kReserveEntries));
  assert(rollover.firstRemoved == 0);
  assert(logs::stringEntries(0) == 2 && logs::stringEntries(31) == 2 &&
         logs::stringEntries(32) == 3);
  puts("OK: shared NVS recovery, oldest-log eviction, settings preserved, sparse ring, failures, sequence wrap");
}
