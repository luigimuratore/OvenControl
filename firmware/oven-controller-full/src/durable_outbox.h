#pragma once
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <string>
#include "outbox_format.h"

namespace telegram {
// Single worker owns this store; monitoring reads atomic counters only.
template <class Storage> class BasicOutbox {
 public:
  static constexpr int kSlots = 96;
  struct Entry { StoredHeader header; StoredProgress progress; bool occupied = false, loaded = false; };
 private:
  Entry entries[kSlots];
  int64_t owner = 0;
  bool mounted = false;
  uint64_t sequence = 0;
  std::atomic<bool> ready{false};
  std::atomic<uint32_t> pending{0}, errors{0};
  Storage storage;
  static std::string path(int slot, const char *suffix = "dat") {
    char value[32]; snprintf(value,sizeof(value),"/ovtg%d.%s",slot,suffix); return value;
  }
  bool verified(typename Storage::File &file, const StoredHeader &h) {
    uint8_t bytes[512]; uint32_t crc = h.initialChecksum();
    while (file.available()) {
      const size_t n = file.read(bytes, sizeof(bytes)); if (!n) return false;
      crc = checksum(bytes, n, crc); storage.pause();
    }
    return crc == h.crc;
  }
 public:
  bool begin(int64_t primaryChat) {
    owner = primaryChat;
    mounted = storage.begin();
    if (!mounted) { ++errors; return false; }
    for (int i = 0; i < kSlots; ++i) {
      // Only our incomplete staging files are disposable.
      if (storage.exists(path(i, "tmp"))) storage.remove(path(i, "tmp"));
      auto file = storage.open(path(i), "r");
      if (!file) continue;
      StoredHeader h;
      const bool valid = file.read(reinterpret_cast<uint8_t *>(&h), sizeof(h)) == sizeof(h) && h.valid(owner, file.size()) && verified(file, h);
      file.close();
      if (!valid) { ++errors; storage.remove(path(i)); storage.remove(path(i, "ack")); continue; }
      auto &entry = entries[i]; entry.occupied = true; entry.header = h; ++pending;
      sequence = std::max(sequence, h.sequence);
      auto ack = storage.open(path(i, "ack"), "r");
      if (ack) {
        StoredProgress candidate;
        while (ack.read(reinterpret_cast<uint8_t *>(&candidate), sizeof(candidate)) == sizeof(candidate))
          if (candidate.valid()) entry.progress = candidate;
      }
      if (entry.progress.index == 255) erase(i);
    }
    ready = true; return true;
  }
  bool active() const { return ready.load(); }
  uint32_t count() const { return pending.load(); }
  uint32_t failures() const { return errors.load(); }
  const Entry &entry(int i) const { return entries[i]; }
  int next(StoredKind kind) const {
    int selected = -1;
    for (int i = 0; i < kSlots; ++i) if (entries[i].occupied && !entries[i].loaded && entries[i].header.kind == kind &&
        (selected < 0 || entries[i].header.sequence < entries[selected].header.sequence)) selected = i;
    return selected;
  }
  void loaded(int slot) { if (slot >= 0) entries[slot].loaded = true; }
  int put(StoredKind kind, const void *metadata, size_t size, const void *payload = nullptr, size_t payloadSize = 0) {
    if (!mounted) return -1;
    if (!metadata || !size || size>16384 || payloadSize>4321*32 || (payloadSize&&!payload)) {++errors;return -1;}
    int slot = -1; for (int i = 0; i < kSlots; ++i) if (!entries[i].occupied) { slot = i; break; }
    if (slot < 0 || storage.availableBytes() < size + payloadSize + 65536) { ++errors; return -1; }
    // Reusing a slot must never inherit a previous recipient's ACK/tombstone.
    if (storage.exists(path(slot, "ack")) && !storage.remove(path(slot, "ack"))) { ++errors; return -1; }
    StoredHeader h; h.owner = owner; h.kind = kind; h.metadata = size; h.payload = payloadSize; h.sequence = ++sequence;
    h.utc = storage.now();
    h.crc = checksum(metadata, size, h.initialChecksum());
    if (payloadSize) h.crc = checksum(payload, payloadSize, h.crc);
    auto file = storage.open(path(slot, "tmp"), "w");
    const bool written = file && file.write(reinterpret_cast<const uint8_t *>(&h), sizeof(h)) == sizeof(h) &&
      file.write(static_cast<const uint8_t *>(metadata), size) == size &&
      (!payloadSize || file.write(static_cast<const uint8_t *>(payload), payloadSize) == payloadSize);
    file.flush(); file.close();
    if (!written || !storage.sync(path(slot, "tmp")) || !storage.rename(path(slot, "tmp"), path(slot))) { ++errors; storage.remove(path(slot, "tmp")); return -1; }
    entries[slot] = Entry{}; entries[slot].occupied = true; entries[slot].header = h; ++pending;
    return slot;
  }
  bool read(int slot, void *metadata, size_t size, void *payload = nullptr, size_t payloadSize = 0) {
    const auto &h = entries[slot].header;
    if (h.metadata != size || h.payload != payloadSize) return false;
    auto file = storage.open(path(slot), "r");
    return file && file.seek(sizeof(StoredHeader)) && file.read(static_cast<uint8_t *>(metadata), size) == size &&
      (!payloadSize || file.read(static_cast<uint8_t *>(payload), payloadSize) == payloadSize);
  }
  bool mark(int slot, uint32_t index, bool ambiguous, bool failed = false) {
    if (slot < 0) return true; // RAM fallback has no durable record to replay.
    auto &entry = entries[slot];
    if (entry.progress.index == index && entry.progress.ambiguous == unsigned(ambiguous) && entry.progress.failed == unsigned(failed)) return true;
    StoredProgress progress; progress.index = index; progress.ambiguous = ambiguous; progress.failed = failed; progress.seal();
    auto file = storage.open(path(slot, "ack"), "a");
    if (!file) { ++errors; return false; }
    // A torn final record must not misalign future append records.
    const size_t padding = (sizeof(progress) - file.size() % sizeof(progress)) % sizeof(progress);
    const uint8_t zeros[sizeof(progress)]{};
    const bool ok = (!padding || file.write(zeros, padding) == padding) &&
      file.write(reinterpret_cast<const uint8_t *>(&progress), sizeof(progress)) == sizeof(progress);
    file.flush(); file.close();
    if (!ok || !storage.sync(path(slot, "ack"))) { ++errors; return false; }
    entry.progress = progress; return true;
  }
  void erase(int slot) {
    if (slot < 0 || !entries[slot].occupied) return;
    mark(slot, 255, false);
    if (storage.remove(path(slot))) {
      storage.remove(path(slot, "ack")); entries[slot] = Entry{}; --pending;
    } else { entries[slot].loaded = true; ++errors; }
  }
};
} // namespace telegram
