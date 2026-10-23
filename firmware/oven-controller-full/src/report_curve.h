#pragma once
#include <cstddef>
#include <cstdint>

namespace reports {
// Copy only the ended cycle, in acquisition order, including millis() rollover.
// The destination belongs to the report, independently of the dashboard ring.
template <typename Sample>
size_t copyCurve(const Sample *ring, size_t capacity, size_t head, size_t count,
                 uint32_t start, uint32_t end, Sample *out, size_t outCapacity,
                 bool &partial) {
  partial = true;
  if (!ring || !out || !capacity || !outCapacity) return 0;
  if (count > capacity) count = capacity;
  head %= capacity;
  const uint32_t span = uint32_t(end - start);
  size_t copied = 0; bool truncated = false;
  size_t first = 0;
  // A sample acquired just before START can have the same millisecond. Prefer
  // the explicit start marker so it cannot leak into the next session's curve.
  for (size_t i = 0; i < count; ++i) {
    const auto &sample = ring[(head + capacity - count + i) % capacity];
    if (sample.ms == start && (sample.flags & 0x10)) { first = i; break; }
  }
  for (size_t i = first; i < count; ++i) {
    const auto &sample = ring[(head + capacity - count + i) % capacity];
    if (uint32_t(sample.ms - start) > span) continue;
    if (copied == outCapacity) { truncated = true; break; }
    out[copied++] = sample;
  }
  partial = truncated || !copied || !(out[0].flags & 0x10);
  return copied;
}
} // namespace reports
