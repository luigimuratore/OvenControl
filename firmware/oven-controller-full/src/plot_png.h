#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "report_pdf.h"

// Palette PNG with stored DEFLATE blocks: bounded memory, no chart service or
// filesystem. Only the Telegram worker renders; the loop supplies a snapshot.
namespace plotpng {
constexpr unsigned kWidth = 960, kHeight = 480;
constexpr size_t kRasterBytes = (kWidth + 1) * kHeight;
constexpr size_t kCapacity = kRasterBytes + 4096;
static const uint8_t kPalette[] = {13,21,29, 229,237,243, 120,145,163, 41,60,75, 74,163,255, 53,190,145};
class Canvas {
  uint8_t *pixels;
public:
  explicit Canvas(uint8_t *data) : pixels(data) { memset(pixels, 0, kRasterBytes); }
  void dot(int x, int y, uint8_t color) {
    if (x >= 0 && x < int(kWidth) && y >= 0 && y < int(kHeight)) pixels[size_t(y) * (kWidth + 1) + 1 + x] = color;
  }
  void line(int x, int y, int endX, int endY, uint8_t color, bool thick = false) {
    const int dx = std::abs(endX - x), sx = x < endX ? 1 : -1;
    const int dy = -std::abs(endY - y), sy = y < endY ? 1 : -1; int error = dx + dy;
    for (;;) {
      dot(x, y, color); if (thick) { dot(x + 1, y, color); dot(x, y + 1, color); }
      if (x == endX && y == endY) break;
      const int e = 2 * error;
      if (e >= dy) { error += dy; x += sx; }
      if (e <= dx) { error += dx; y += sy; }
    }
  }
  void text(int x, int y, const char *value, uint8_t color = 1, unsigned scale = 2) {
    static const uint8_t font[][7] = {
      {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},
      {31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
      {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},
      {31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,18,18,12},
      {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
      {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
      {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
    };
    for (const char *p = value; *p; ++p, x += 6 * scale) {
      char c = *p; if (c >= 'a' && c <= 'z') c -= 32;
      const int index = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : -1;
      for (unsigned row = 0; row < 7; ++row) for (unsigned col = 0; col < 5; ++col) {
        bool on = index >= 0 && (font[index][row] & (1 << (4 - col)));
        if (c == '.') on = row == 6 && col == 2;
        if (c == ':') on = (row == 2 || row == 5) && col == 2;
        if (c == '-') on = row == 3;
        if (c == '/') on = col == 4 - row * 4 / 6;
        if (on) for (unsigned a = 0; a < scale; ++a) for (unsigned b = 0; b < scale; ++b) dot(x + col * scale + a, y + row * scale + b, color);
      }
    }
  }
};
inline uint32_t crc32(const uint8_t *data, size_t count, void (*yield)()) {
  uint32_t c = 0xffffffff;
  for (size_t i = 0; i < count; ++i) {
    c ^= data[i]; for (unsigned b = 0; b < 8; ++b) c = (c >> 1) ^ ((c & 1) ? 0xedb88320 : 0);
    if (yield && !(i % 16384)) yield();
  }
  return c ^ 0xffffffff;
}
inline size_t generate(char *output, size_t capacity, uint8_t *raster, size_t rasterSize,
                       const reportpdf::Point *points, size_t count, uint32_t start, uint32_t end,
                       void (*yield)() = nullptr) {
  if (!output || capacity < kCapacity || !raster || rasterSize < kRasterBytes || (count && !points)) return 0;
  Canvas c(raster); char text[80];
  c.text(30, 22, "OVENCONTROL / GRAFICO ATTUALE", 1, 3);
  c.text(30, 62, "TEMPERATURA C", 2);
  c.line(560, 70, 592, 70, 4, true); c.text(606, 62, "TARGET", 1);
  c.line(730, 70, 762, 70, 5, true); c.text(776, 62, "MEDIA PT100", 1);
  float lo = INFINITY, hi = -INFINITY;
  for (size_t i = 0; i < count; ++i) for (float v : {points[i].mean, points[i].target})
    if (std::isfinite(v)) { lo = fminf(lo, v); hi = fmaxf(hi, v); }
  if (!std::isfinite(lo)) { lo = 0; hi = 1; }
  const float tick = fmaxf(1, ceilf((hi - lo + 2) / 7));
  lo = floorf((lo - 1) / tick) * tick; hi = ceilf((hi + 1) / tick) * tick;
  const int left = 84, right = 930, top = 110, bottom = 400;
  for (float v = lo; v <= hi; v += tick) {
    const int y = bottom - int((v - lo) / (hi - lo) * (bottom - top));
    c.line(left, y, right, y, 3); snprintf(text, sizeof(text), "%.0f", double(v)); c.text(12, y - 7, text, 2);
  }
  const uint32_t duration = uint32_t(end - start); const double span = std::max(1000u, duration);
  for (unsigned i = 0; i <= 6; ++i) {
    const int x = left + (right - left) * i / 6; c.line(x, top, x, bottom, 3);
    snprintf(text, sizeof(text), "%.1f", span * i / 360000); c.text(x - 20, 418, text, 2);
  }
  c.text(318, 452, "TEMPO VISUALIZZATO / MIN", 2);
  for (unsigned series = 0; series < 2; ++series) {
    bool previous = false; int px = 0, py = 0; uint32_t last = 0;
    for (size_t i = 0; i < count; ++i) {
      const float v = series ? points[i].mean : points[i].target;
      const uint32_t offset = uint32_t(points[i].ms - start);
      if (!std::isfinite(v) || offset > duration) { previous = false; continue; }
      const int x = left + int(offset / span * (right - left));
      const int y = bottom - int((v - lo) / (hi - lo) * (bottom - top));
      if (previous && uint32_t(points[i].ms - last) <= 25000 && (series || !(points[i].flags & 16))) c.line(px, py, x, y, series ? 5 : 4, true);
      else { c.dot(x, y, series ? 5 : 4); c.dot(x + 1, y, series ? 5 : 4); }
      px = x; py = y; last = points[i].ms; previous = true;
    }
    if (yield) yield();
  }
  if (!count) c.text(240, 240, "STORICO NON ANCORA DISPONIBILE", 2);
  auto *out = reinterpret_cast<uint8_t *>(output); size_t at = 0;
  const auto byte = [&](uint8_t v) { out[at++] = v; };
  const auto big = [&](uint32_t v) { byte(v >> 24); byte(v >> 16); byte(v >> 8); byte(v); };
  const auto chunk = [&](const char *type, const uint8_t *data, size_t n) {
    big(uint32_t(n)); const size_t begin = at; memcpy(out + at, type, 4); at += 4;
    if (n) { memcpy(out + at, data, n); at += n; }
    big(crc32(out + begin, n + 4, yield));
  };
  static const uint8_t signature[] = {137,80,78,71,13,10,26,10}; memcpy(out, signature, 8); at = 8;
  const uint8_t header[] = {0,0,uint8_t(kWidth >> 8),uint8_t(kWidth),0,0,uint8_t(kHeight >> 8),uint8_t(kHeight),8,3,0,0,0};
  chunk("IHDR", header, sizeof(header)); chunk("PLTE", kPalette, sizeof(kPalette));
  const size_t blocks = (kRasterBytes + 65534) / 65535;
  big(uint32_t(kRasterBytes + blocks * 5 + 6)); const size_t begin = at;
  memcpy(out + at, "IDAT", 4); at += 4; byte(0x78); byte(0x01);
  uint32_t a = 1, b = 0;
  for (size_t pos = 0; pos < kRasterBytes;) {
    const unsigned n = std::min(size_t(65535), kRasterBytes - pos); byte(pos + n == kRasterBytes ? 1 : 0);
    byte(n); byte(n >> 8); byte(~n); byte((~n) >> 8);
    memcpy(out + at, raster + pos, n); at += n;
    for (size_t j = 0; j < n; ++j) { a += raster[pos + j]; b += a; if (!(j % 5552)) { a %= 65521; b %= 65521; } }
    a %= 65521; b %= 65521; pos += n; if (yield) yield();
  }
  big((b << 16) | a); big(crc32(out + begin, at - begin, yield)); chunk("IEND", nullptr, 0);
  return at <= capacity ? at : 0;
}
} // namespace plotpng
