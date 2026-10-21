#include <cassert>
#include <iostream>
#include "../src/rtd_conversion.h"
#include <vector>
// Model a slow but in-spec 50 Hz ADC (66 ms) and an input filter needing
// 2 ms to settle. Early reads return stale ADC data / a false RTDIN fault.
struct Device {
  uint32_t now = 0, biasAt = 0, shotAt = 0, openAt = 0, finishAt = 0;
  unsigned stops = 0;
  bool biasOn = false, open = false, closed = false;
  bool rtdFault = false;
  std::vector<char> calls;
  void bias() { biasOn = true; biasAt = now; open = closed = false; calls.push_back('b'); }
  void oneShot() {
    assert(biasOn && uint32_t(now - biasAt) >= 10);
    shotAt = now; calls.push_back('s');
  }
  uint16_t readRaw() { calls.push_back('r'); return ((uint32_t(now - shotAt) >= 66 ? 11678 : 8303) << 1) | rtdFault; }
  void startFault() { assert(biasOn); open = true; openAt = now; calls.push_back('o'); }
  void finishFault() { assert(open && uint32_t(now - openAt) >= 2); closed = true; finishAt = now; calls.push_back('c'); }
  uint8_t faultBits() {
    assert(closed && uint32_t(now - finishAt) >= 1);
    calls.push_back('f'); return uint32_t(now - openAt) >= 2 ? 0 : 0x08;
  }
  void stop() { biasOn = false; open = false; ++stops; calls.push_back('x'); }
};
void run(uint32_t start, uint32_t cadence) {
  probes::Conversion a, b; Device d1, d2;
  d1.now = d2.now = start; a.start(d1, start); b.start(d2, start);
  bool complete = false;
  for (uint32_t elapsed = 0; elapsed < 200 && !complete; elapsed += cadence) {
    d1.now = d2.now = start + elapsed;
    complete = a.tick(d1, d1.now);
    assert(b.tick(d2, d2.now) == complete);
  }
  assert(complete && a.raw == 11678 && b.raw == 11678 && a.fault == 0 && b.fault == 0);
  assert(!a.rawFault && !b.rawFault);
  assert(d1.stops == 1 && d2.stops == 1 && !d1.biasOn && !d2.biasOn);
  assert(d1.calls == std::vector<char>({'b','s','r','o','c','f','x'}) && d1.calls == d2.calls);
  assert(!a.tick(d1, d1.now + 1000));
  d1.now += 1000; d1.rtdFault = true; a.start(d1, d1.now);
  for (unsigned i = 0; i <= 100; ++i) {
    ++d1.now;
    if (a.tick(d1, d1.now)) { assert(d1.stops == 2 && a.raw == 11678 && a.rawFault && a.fault == 0); return; }
  }
  assert(false);
}
int main() {
  for (uint32_t cadence : {1u, 3u, 10u}) {
    run(0, cadence); run(UINT32_MAX - 30, cadence);
  }
  std::cout << "Async PT100: fresh 66 ms ADC results, manual filter settling, parallel pairs, restart, delayed ticks and millis rollover passed\n";
}
