#pragma once
#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace plotpng {
// Fixed-Huffman DEFLATE with distance-one matches. Charts have long runs of
// background/grid colors: lossless compression needs no dictionary allocation,
// library or large stack. Return 0 on output exhaustion; caller can store blocks.
inline size_t deflateRuns(uint8_t *out, size_t capacity, const uint8_t *data, size_t size,
                          void (*yield)() = nullptr) {
  if (!out || capacity < 8 || (size && !data)) return 0;
  size_t at = 2; out[0] = 0x78; out[1] = 0x01;
  uint32_t bits = 0; unsigned count = 0; bool full = false;
  const auto put = [&](unsigned value, unsigned n) {
    if (full) return;
    bits |= uint32_t(value) << count; count += n;
    while (count >= 8) {
      if (at >= capacity) { full = true; return; }
      out[at++] = uint8_t(bits); bits >>= 8; count -= 8;
    }
  };
  const auto symbol = [&](unsigned value) {
    unsigned code, n;
    if (value < 144) { code = value + 0x30; n = 8; }
    else if (value < 256) { code = value - 144 + 0x190; n = 9; }
    else if (value < 280) { code = value - 256; n = 7; }
    else { code = value - 280 + 0xc0; n = 8; }
    unsigned reversed = 0;
    for (unsigned i = 0; i < n; ++i) { reversed = (reversed << 1) | (code & 1); code >>= 1; }
    put(reversed, n);
  };
  static const unsigned base[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
  static const unsigned extra[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
  put(3, 3); // Final block, fixed Huffman codes.
  size_t sinceYield = 0;
  for (size_t pos = 0; pos < size && !full;) {
    size_t run = 0;
    if (pos && data[pos] == data[pos - 1])
      while (run < 258 && pos + run < size && data[pos + run] == data[pos - 1]) ++run;
    if (run >= 3) {
      unsigned code = 0;
      while (code < 28 && base[code + 1] <= run) ++code;
      symbol(257 + code); put(unsigned(run - base[code]), extra[code]); put(0, 5);
      pos += run; sinceYield += run;
    } else { symbol(data[pos++]); ++sinceYield; }
    if (sinceYield >= 16384) { if (yield) yield(); sinceYield = 0; }
  }
  if (full) return 0;
  symbol(256); if (full) return 0;
  if (count) put(0, 8 - count);
  if (full || at + 4 > capacity) return 0;
  uint32_t a = 1, b = 0;
  for (size_t pos = 0; pos < size;) {
    const size_t end = std::min(size, pos + 5552);
    for (; pos < end; ++pos) { a += data[pos]; b += a; }
    a %= 65521; b %= 65521; if (yield) yield();
  }
  const uint32_t checksum = (b << 16) | a;
  for (int shift = 24; shift >= 0; shift -= 8) out[at++] = uint8_t(checksum >> shift);
  return at;
}
} // namespace plotpng
