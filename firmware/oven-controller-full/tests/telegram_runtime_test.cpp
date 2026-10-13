#include "../src/telegram_runtime.h"
#include <cassert>
#include <cstdio>

int main() {
  using namespace telegram;
  // A fresh handshake uses two 16 KiB records, not a single 32 KiB block.
  assert(tlsHeapReady(100000, 24000, false, 16384));
  assert(tlsHeapReady(60000, 18432, false, 16384));
  assert(!tlsHeapReady(59999, 18432, false, 16384));
  assert(!tlsHeapReady(100000, 18431, false, 16384));
  // Regression: its existing records consume heap and split free blocks.
  // Reusing the connected session must not destroy it at each poll.
  assert(tlsHeapReady(36412, 8000, true, 16384));
  assert(tlsHeapReady(24000, 0, true, 16384));
  assert(!tlsHeapReady(23999, 8000, true, 16384));
  assert(!tlsHeapReady(36412, 8000, false, 16384));
  assert(!tlsHeapReady(100000, 24000, false, 32768));

  RetryLane docs, messages, polling;
  const uint32_t now = 100;
  docs.fail(now, 60);
  assert(!docs.ready(now + 5000));
  assert(messages.ready(now + 5000) && polling.ready(now + 5000));
  polling.success(now + 5000, 2000);
  assert(!polling.ready(now + 6999) && polling.ready(now + 7000));
  assert(!docs.ready(now + 59999) && docs.ready(now + 60000));
  messages.fail(now + 5000);
  assert(!messages.ready(now + 9999) && messages.ready(now + 10000));
  assert(polling.ready(now + 7000));
  docs.success(now + 60000); docs.fail(now + 60000);
  assert(docs.ready(now + 65000)); // Success resets only this lane's backoff.
  RetryLane wrap;
  const uint32_t start = UINT32_MAX - 100;
  assert(wrap.ready(start)); wrap.fail(start);
  assert(!wrap.ready(uint32_t(start + 4999)) && wrap.ready(uint32_t(start + 5000)));
  puts("Telegram: handshake/reuse memory budgets, SDK record sizes, independent retry lanes and wrap passed");
}
