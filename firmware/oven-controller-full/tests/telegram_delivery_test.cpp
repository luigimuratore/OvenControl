#include "../src/telegram_delivery.h"
#include <cassert>
#include <cstring>

int main() {
  using namespace telegram;
  const char *reply = "{\"ok\":true,\"result\":{\"message_id\":91}}";
  char buffer[128];
  int reads = 0;
  // Regression: fetch_headers already received the entire body. The IDF
  // complete flag is true, but read must still drain its cached response.
  auto body = readDocumentBody(buffer, sizeof(buffer),
    [&](char *out, size_t capacity) {
      ++reads; assert(capacity >= strlen(reply));
      memcpy(out, reply, strlen(reply)); return int(strlen(reply));
    }, [] { return true; }, [] { return true; });
  assert(reads == 1 && body.complete && body.bytes == strlen(reply));
  assert(strcmp(buffer, reply) == 0);
  assert(documentResult(true, 200, true, true, 91) == DocumentResult::Confirmed);

  size_t at = 0;
  body = readDocumentBody(buffer, sizeof(buffer),
    [&](char *out, size_t) {
      const size_t n = strlen(reply) - at > 7 ? 7 : strlen(reply) - at;
      memcpy(out, reply + at, n); at += n; return int(n);
    }, [&] { return at == strlen(reply); }, [] { return true; });
  assert(body.complete && strcmp(buffer, reply) == 0);

  body = readDocumentBody(buffer, sizeof(buffer),
    [](char *, size_t) { return -1; }, [] { return false; }, [] { return true; });
  assert(!body.complete && body.bytes == 0 && buffer[0] == '\0');
  assert(documentResult(true, -1, false, false, 0) == DocumentResult::Uncertain);
  assert(documentResult(true, 200, false, false, 0) == DocumentResult::Uncertain);
  assert(documentResult(true, 200, true, true, 0) == DocumentResult::Uncertain);
  assert(documentResult(true, 503, true, false, 0) == DocumentResult::Uncertain);
  assert(documentResult(true, 429, true, false, 0) == DocumentResult::Retry);
  assert(documentResult(true, 403, true, false, 0) == DocumentResult::Rejected);
  // Offline, failed connection, partial PDF and alarm preemption before the
  // final boundary are safe to retry. A failed final write is uncertain.
  assert(documentResult(false, -1, false, false, 0) == DocumentResult::Retry);
  assert(documentResult(false, -2, false, false, 0) == DocumentResult::Retry);

  // Deadline / unavailable checks and bounded response buffer.
  reads = 0;
  body = readDocumentBody(buffer, sizeof(buffer),
    [&](char *, size_t) { ++reads; return 0; }, [] { return false; }, [] { return false; });
  assert(!reads && !body.complete && !body.bytes);
  body = readDocumentBody(buffer, 1,
    [&](char *, size_t) { ++reads; return 0; }, [] { return true; }, [] { return true; });
  assert(!reads && !body.complete && !body.bytes);

  RecentReportCycles cycles;
  // Several consecutive cycles may be queued. A later duplicate STOP for
  // either cycle cannot enqueue another PDF, including start at uptime zero.
  assert(!cycles.contains(19, 0)); cycles.remember(19, 0);
  cycles.remember(19, 794418);
  assert(cycles.contains(19, 0) && cycles.contains(19, 794418));
  assert(!cycles.contains(20, 794418) && !cycles.contains(19, 794419));
  // A resumed cycle may later produce a final report; repeated copies of each
  // terminal event must still be suppressed.
  cycles.remember(21,1000,62000);
  assert(cycles.contains(21,1000,62000));
  assert(!cycles.contains(21,1000,782000));
  cycles.remember(21,1000,782000);
  assert(cycles.contains(21,1000,62000) && cycles.contains(21,1000,782000));
}
