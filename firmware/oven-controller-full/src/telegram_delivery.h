#pragma once
#include <cstddef>
#include <cstdint>

namespace telegram {
enum class DocumentResult { Confirmed, Retry, Rejected, Uncertain };

// sendDocument has no idempotency key. Retry only if the final multipart
// boundary could not have reached Telegram, or Telegram explicitly rejected it.
inline DocumentResult documentResult(bool mayHaveDelivered, int code, bool validResponse,
                                     bool apiOk, int64_t messageId) {
  if (code == 200 && validResponse && apiOk && messageId > 0) return DocumentResult::Confirmed;
  if (code == 429) return DocumentResult::Retry;
  if (code >= 400 && code < 500) return DocumentResult::Rejected;
  return mayHaveDelivered ? DocumentResult::Uncertain : DocumentResult::Retry;
}

struct BodyRead { size_t bytes = 0; bool complete = false; };
template <typename Read, typename Complete, typename Ready>
BodyRead readDocumentBody(char *buffer, size_t capacity, Read read, Complete complete, Ready ready) {
  BodyRead result;
  if (!capacity) return result;
  // fetch_headers may have received the complete response AND cached its body.
  // complete() describes network reception, not consumption of that cache.
  // Always call read at least once before consulting complete().
  while (result.bytes < capacity - 1 && ready()) {
    const int n = read(buffer + result.bytes, capacity - 1 - result.bytes);
    if (n <= 0) { result.complete = n == 0 && complete(); break; }
    result.bytes += size_t(n);
    if (complete()) { result.complete = true; break; }
  }
  buffer[result.bytes] = '\0';
  return result;
}

// Accessed only by the producer (Arduino loop), never by the network worker.
class RecentReportCycles {
  struct Key { uint32_t boot = 0, start = 0; bool used = false; };
  Key keys[16]{};
  size_t next = 0;
public:
  bool contains(uint32_t boot, uint32_t start) const {
    for (const auto &key : keys) if (key.used && key.boot == boot && key.start == start) return true;
    return false;
  }
  void remember(uint32_t boot, uint32_t start) {
    keys[next].boot = boot; keys[next].start = start; keys[next].used = true;
    next = (next + 1) % 16;
  }
};
} // namespace telegram
