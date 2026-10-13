#pragma once
#include <cstddef>
#include <cstdint>
#include "telegram_policy.h"

namespace telegram {
// Existing TLS record buffers already occupy internal heap. Reusing that
// session needs a working reserve, not the allocations of a new handshake.
inline bool tlsHeapReady(size_t freeInternal, size_t largestBlock, bool connected, size_t recordBytes) {
  return connected ? freeInternal >= 24000 :
    freeInternal >= 60000 && largestBlock >= recordBytes + 2048;
}
class RetryLane {
  uint32_t deadline = 0;
  unsigned failures = 0;
  bool scheduled = false;
public:
  bool ready(uint32_t now) const { return !scheduled || int32_t(now - deadline) >= 0; }
  void success(uint32_t now, uint32_t wait = 0) { failures = 0; deadline = now + wait; scheduled = true; }
  void fail(uint32_t now, unsigned serverSeconds = 0) {
    deadline = now + retryMs(++failures, serverSeconds); scheduled = true;
  }
};
} // namespace telegram
