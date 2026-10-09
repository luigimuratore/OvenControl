#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include "cycle_report.h"

// Small, dependency-free PDF writer. The caller owns a bounded PSRAM buffer;
// no GPIO, sensor, Preferences or shared history access occurs while rendering.
namespace reportpdf {
constexpr size_t kCapacity = 768 * 1024;
struct Point { uint32_t ms; float mean, target; };
inline const char *outcome(reports::Outcome value) {
  return value == reports::Outcome::Completed ? "CICLO COMPLETATO" :
         value == reports::Outcome::Fault ? "CICLO IN ALLARME" : "CICLO FERMATO";
}
inline void duration(uint32_t ms, char *out, size_t size) {
  const unsigned seconds = ms / 1000;
  snprintf(out, size, "%02u:%02u:%02u", seconds / 3600, seconds / 60 % 60, seconds % 60);
}
// Europe/Rome without changing the process/global timezone used by NTP.
inline time_t romeTime(uint64_t utc) {
  time_t epoch = static_cast<time_t>(utc); tm date{}; gmtime_r(&epoch, &date);
  bool summer = date.tm_mon > 2 && date.tm_mon < 9;
  if (date.tm_mon == 2 || date.tm_mon == 9) {
    const int lastSunday = 31 - (date.tm_wday + 31 - date.tm_mday) % 7;
    const bool after = date.tm_mday > lastSunday || (date.tm_mday == lastSunday && date.tm_hour >= 1);
    summer = date.tm_mon == 2 ? after : !after;
  }
  return epoch + (summer ? 7200 : 3600);
}
inline void date(uint64_t utc, char *out, size_t size, bool file = false) {
  if (!utc) { snprintf(out, size, "Orario non sincronizzato"); return; }
  const time_t epoch = romeTime(utc); tm local{}; gmtime_r(&epoch, &local);
  strftime(out, size, file ? "%Y-%m-%d_%H-%M-%S" : "%d/%m/%Y %H:%M:%S (Italia)", &local);
}
inline void filename(const reports::Snapshot &r, char *out, size_t size) {
  char name[65]{}; size_t used = 0;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(r.recipeName); *p && used < 48; ++p) {
    const bool ascii = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-';
    if (ascii) name[used++] = *p;
    else if (used && name[used - 1] != '_') name[used++] = '_';
  }
  while (used && name[used - 1] == '_') name[--used] = 0;
  char stamp[64];
  if (r.endUtc) date(r.endUtc, stamp, sizeof(stamp), true);
  else snprintf(stamp, sizeof(stamp), "senza_orario_avvio-%u_%u", unsigned(r.boot), unsigned(r.startMs));
  const char *prefix = r.outcome == reports::Outcome::Completed ? "Completed" :
    r.outcome == reports::Outcome::Stopped ? "Stopped" : "Error";
  snprintf(out, size, "%s_%s_%s.pdf", prefix, used ? name : "Programma", stamp);
}
class Writer {
  char *buffer; size_t capacity, used = 0;
  bool good = true;
  size_t offsets[32]{};
  unsigned pages = 0, streamId = 0;
  size_t lengthAt = 0, contentAt = 0;
  void append(const char *data, size_t n) {
    if (!good || n > capacity - used) { good = false; return; }
    memcpy(buffer + used, data, n); used += n;
  }
  void put(const char *format, ...) {
    char line[512]; va_list args; va_start(args, format);
    const int n = vsnprintf(line, sizeof(line), format, args); va_end(args);
    if (n < 0 || size_t(n) >= sizeof(line)) { good = false; return; }
    append(line, n);
  }
  void object(unsigned id) { offsets[id] = used; put("%u 0 obj\n", id); }
  // PDF Helvetica /WinAnsiEncoding, including Italian Latin-1 accents.
  static size_t latin(const char *input, char *out, size_t size) {
    size_t n = 0;
    const auto *p = reinterpret_cast<const unsigned char *>(input);
    while (*p && n + 1 < size) {
      unsigned c = *p++;
      if (c >= 0xc2 && c <= 0xc3 && (*p & 0xc0) == 0x80) c = ((c & 31) << 6) | (*p++ & 63);
      else if (c >= 128) { while ((*p & 0xc0) == 0x80) ++p; c = '?'; }
      if (c < 32) c = ' ';
      out[n++] = char(c);
    }
    out[n] = 0; return n;
  }
  void encoded(const char *value) {
    char converted[1024]; latin(value, converted, sizeof(converted));
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(converted); *p; ++p) {
      if (*p == '(' || *p == ')' || *p == '\\') put("\\%c", *p);
      else if (*p >= 128) put("\\%03o", *p);
      else put("%c", *p);
    }
  }
public:
  Writer(char *data, size_t size) : buffer(data), capacity(size) {
    put("%%PDF-1.4\n");
    object(1); put("<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    object(3); put("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>\nendobj\n");
    object(4); put("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>\nendobj\n");
  }
  void text(float x, float y, const char *value, float font = 10, bool bold = false) {
    put("0.125 0.204 0.259 rg BT /F%u %.1f Tf 1 0 0 1 %.1f %.1f Tm (", bold ? 2 : 1, font, x, 842 - y);
    encoded(value); put(") Tj ET\n");
  }
  float wrap(float x, float y, const char *value, unsigned columns = 92, float font = 10, bool words = true) {
    char converted[1024]; latin(value, converted, sizeof(converted));
    // latin() has already converted accents; emit lines as PDF escaped bytes.
    const size_t total = strlen(converted); size_t pos = 0;
    while (pos < total) {
      size_t n = 0; float width = 0;
      while (pos + n < total && n < columns) {
        const unsigned c = static_cast<unsigned char>(converted[pos + n]);
        float glyph = c >= 'A' && c <= 'Z' ? .778f : .556f;
        if (c == 'W' || c == '@') glyph = 1;
        else if (c == 'M' || c == 'm') glyph = .889f;
        else if (c == 'w') glyph = .778f;
        else if (c == ' ' || c == 'i' || c == 'l' || c == '.' || c == ':' || c == ',') glyph = .278f;
        width += glyph * font;
        if (width > 507 && n) break;
        ++n;
      }
      if (words && pos + n < total) { size_t end = n; while (end && converted[pos + end] != ' ') --end; if (end) n = end; }
      put("0.125 0.204 0.259 rg BT /F1 %.1f Tf 1 0 0 1 %.1f %.1f Tm (", font, x, 842 - y);
      for (size_t i = 0; i < n; ++i) {
        const unsigned c = static_cast<unsigned char>(converted[pos + i]);
        if (c == '(' || c == ')' || c == '\\') put("\\%c", c);
        else if (c >= 128) put("\\%03o", c);
        else put("%c", c);
      }
      put(") Tj ET\n"); pos += n; while (converted[pos] == ' ') ++pos; y += font * 1.4f;
    }
    return y;
  }
  void line(float x1, float y1, float x2, float y2, const char *color = "0.86 0.90 0.92", float width = 0.6f) {
    put("%s RG %.1f w %.1f %.1f m %.1f %.1f l S\n", color, width, x1, 842 - y1, x2, 842 - y2);
  }
  void page(const char *title, const reports::Snapshot &r, unsigned total) {
    if (pages) endPage();
    streamId = 5 + pages * 2; ++pages;
    object(streamId); put("<< /Length "); lengthAt = used; put("0000000000 >>\nstream\n"); contentAt = used;
    text(44, 38, "OVEN CONTROLLER / REPORT DI CICLO", 9, true);
    wrap(44, 76, title, 48, 18, false);
    line(44, 109, 551, 109);
    char footer[150]; snprintf(footer, sizeof(footer), "Ciclo %u-%u-%u  |  Telegram  |  %u / %u",
      unsigned(r.boot), unsigned(r.startMs), unsigned(r.endMs), pages, total);
    line(44, 798, 551, 798); text(44, 815, footer, 8);
  }
  void endPage() {
    const size_t length = used - contentAt;
    char digits[16]; snprintf(digits, sizeof(digits), "%010u", unsigned(length));
    if (good) memcpy(buffer + lengthAt, digits, 10);
    put("\nendstream\nendobj\n");
    object(streamId + 1);
    put("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents %u 0 R >>\nendobj\n", streamId);
  }
  void pair(float x, float y, const char *label, const char *value) {
    text(x, y, label, 8); text(x, y + 15, value, 10, true);
  }
  size_t finish() {
    endPage(); object(2); put("<< /Type /Pages /Count %u /Kids [", pages);
    for (unsigned i = 0; i < pages; ++i) put("%u 0 R ", 6 + i * 2);
    put("] >>\nendobj\n");
    const size_t xref = used; const unsigned count = 5 + pages * 2;
    put("xref\n0 %u\n0000000000 65535 f \n", count);
    for (unsigned i = 1; i < count; ++i) put("%010u 00000 n \n", unsigned(offsets[i]));
    put("trailer\n<< /Size %u /Root 1 0 R >>\nstartxref\n%u\n%%%%EOF\n", count, unsigned(xref));
    return good ? used : 0;
  }
};
inline void value(float n, char *out, size_t size, const char *unit = "°C") {
  if (std::isfinite(n)) snprintf(out, size, "%.2f %s", double(n), unit);
  else snprintf(out, size, "Non disponibile");
}
inline void statsPairs(Writer &w, const reports::Stats &s, float y) {
  char a[100], b[100];
  for (unsigned i = 0; i < 2; ++i) {
    snprintf(a, sizeof(a), "PT100 #%u / iniziale > finale", i + 1);
    if (s.valid[i]) snprintf(b, sizeof(b), "%.2f > %.2f °C", double(s.start[i]), double(s.end[i]));
    else snprintf(b, sizeof(b), "Non disponibile");
    w.pair(44 + i * 264, y, a, b);
  }
  y += 43;
  for (unsigned i = 0; i < 2; ++i) {
    snprintf(a, sizeof(a), "PT100 #%u / minimo - massimo", i + 1);
    if (s.valid[i]) snprintf(b, sizeof(b), "%.2f - %.2f °C", double(s.low[i]), double(s.high[i]));
    else snprintf(b, sizeof(b), "Non disponibile");
    w.pair(44 + i * 264, y, a, b);
  }
  y += 43; value(s.maxDelta, a, sizeof(a)); w.pair(44, y, "Differenza massima sonde", a);
  snprintf(a, sizeof(a), "%u / %u", unsigned(s.samples), unsigned(s.invalidPairs));
  w.pair(308, y, "Campioni / coppie non valide", a);
  y += 43;
  value(s.trackingSamples ? s.absoluteErrorSum / s.trackingSamples : NAN, a, sizeof(a));
  w.pair(44, y, "Errore medio assoluto", a);
  value(s.trackingSamples ? s.maxOvershoot : NAN, a, sizeof(a)); w.pair(308, y, "Superamento massimo target", a);
  y += 43; value(s.trackingSamples ? s.maxLag : NAN, a, sizeof(a)); w.pair(44, y, "Ritardo massimo media", a);
  value(s.trackingSamples ? s.dutySum / s.trackingSamples : NAN, a, sizeof(a), "%");
  w.pair(308, y, "PID medio richiesto", a);
  y += 43; value(s.trackingSamples ? 100.0f * s.saturatedSamples / s.trackingSamples : NAN, a, sizeof(a), "%");
  w.pair(44, y, "Campioni al limite comando", a);
}
inline size_t generate(char *buffer, size_t capacity, const reports::Snapshot &r,
                       const Point *points, size_t count, bool partial, void (*yield)() = nullptr) {
  if (!buffer || capacity < 1024 || r.stepCount > reports::kMaxSteps || (count && !points)) return 0;
  Writer w(buffer, capacity); char a[512], b[100]; const unsigned pages = 2 + (r.stepCount + 2) / 3;
  w.page(r.recipeName, r, pages);
  w.text(44, 135, outcome(r.outcome), 12, true);
  w.wrap(44, 158, r.reason, 96, 9);
  date(r.startUtc, a, sizeof(a)); w.pair(44, 205, "Inizio", a);
  date(r.endUtc, a, sizeof(a)); w.pair(308, 205, "Fine", a);
  duration(r.activeMs + r.pausedMs, a, sizeof(a)); w.pair(44, 248, "Tempo ciclo totale", a);
  snprintf(a, sizeof(a), "%u / %u", r.completedSteps, r.stepCount); w.pair(308, 248, "Step completati", a);
  w.text(44, 310, "SETPOINT E MEDIA DELLE PT100", 11, true);
  const float left = 79, right = 543, top = 353, bottom = 609;
  w.line(330, 328, 350, 328, "0.29 0.64 1", 2); w.text(357, 332, "Setpoint", 9);
  w.line(427, 328, 447, 328, "0.21 0.66 0.51", 2); w.text(454, 332, "Media PT100", 9);
  float lo = INFINITY, hi = -INFINITY;
  for (size_t i = 0; i < count; ++i) for (float v : {points[i].mean, points[i].target})
    if (std::isfinite(v)) { lo = fminf(lo, v); hi = fmaxf(hi, v); }
  if (!std::isfinite(lo)) { lo = 0; hi = 1; }
  lo = floorf(lo - 1); hi = ceilf(hi + 1);
  const float span = fmaxf(1000, uint32_t(r.endMs - r.startMs));
  for (unsigned tick = 0; tick <= 5; ++tick) {
    const float yy = bottom - (bottom - top) * tick / 5;
    w.line(left, yy, right, yy); snprintf(a, sizeof(a), "%.1f", double(lo + (hi - lo) * tick / 5)); w.text(44, yy + 3, a, 8);
    const float xx = left + (right - left) * tick / 5;
    w.line(xx, top, xx, bottom); snprintf(a, sizeof(a), "%.1f", double(span * tick / 300000)); w.text(xx - 8, bottom + 19, a, 8);
  }
  w.text(44, 340, "°C", 9); w.text(243, 651, "Tempo dall'inizio (min)", 9);
  for (unsigned series = 0; series < 2; ++series) {
    bool previous = false; float px = 0, py = 0; uint32_t last = 0;
    for (size_t i = 0; i < count; ++i) {
      const float v = series ? points[i].mean : points[i].target;
      const uint32_t elapsed = uint32_t(points[i].ms - r.startMs);
      if (!std::isfinite(v) || elapsed > uint32_t(r.endMs - r.startMs)) { previous = false; continue; }
      const float x = left + elapsed / span * (right - left), y = bottom - (v - lo) / (hi - lo) * (bottom - top);
      const char *color = series ? "0.21 0.66 0.51" : "0.29 0.64 1";
      if (previous && uint32_t(points[i].ms - last) <= 25000) w.line(px, py, x, y, color, 1.1f);
      else w.line(x - 0.5f, y, x + 0.5f, y, color, 1.4f);
      previous = true; px = x; py = y; last = points[i].ms;
      if (yield && !(i % 128)) yield();
    }
  }
  if (!count) w.text(218, 475, "Curva non disponibile", 12);
  snprintf(a, sizeof(a), "%u campioni. %s", unsigned(count), partial ? "Curva parziale: alcuni campioni non disponibili." : "Curva acquisita durante il ciclo e le pause.");
  w.wrap(44, 684, a, 96, 9);
  w.page("Riepilogo completo", r, pages);
  duration(r.activeMs, a, sizeof(a)); w.pair(44, 135, "Tempo attivo", a);
  duration(r.pausedMs, b, sizeof(b)); snprintf(a, sizeof(a), "%u / %s", unsigned(r.pauseCount), b); w.pair(308, 135, "Pause / durata", a);
  snprintf(a, sizeof(a), "Kp %.3f / Ki %.4f / Kd %.2f", double(r.kp), double(r.ki), double(r.kd)); w.pair(44, 178, "PID usato", a);
  snprintf(a, sizeof(a), "%s / %u s / limite %.1f%%", r.ssr ? "SSR" : "Relè", unsigned(r.windowMs / 1000), double(r.maxPower)); w.pair(308, 178, "Comando usato", a);
  statsPairs(w, r.stats, 230);
  w.text(44, 531, "COME LEGGERE I DATI", 10, true);
  w.wrap(44, 554, "Temperature: campioni del ciclo e delle pause. Errore e PID: soli campioni validi di rampa e mantenimento attivi, escluse pause e cooldown. Superamento: sonda più calda rispetto al target istantaneo. Ritardo ed errore medio: media delle due sonde.", 96, 9);
  snprintf(a, sizeof(a), "Il mantenimento conta entrambe le PT100 entro +/-%.0f °C, con letture fresche consecutive. Il comando GPIO/PID non misura la potenza elettrica. La curva può avere interruzioni per letture non valide.", r.version == 1 ? 1.0 : double(profile::kHoldBand));
  w.wrap(44, 624, a, 96, 9);
  w.wrap(44, 694, "Escursione durante il mantenimento: massimo - minimo di ciascuna sonda, incluse le pause. Gli orari e il nome file usano il fuso italiano; in assenza di NTP l'orario resta non disponibile.", 96, 9);
  for (unsigned i = 0; i < r.stepCount; ++i) {
    if (!(i % 3)) w.page("Dettaglio degli step", r, pages);
    const auto &s = r.steps[i]; const float y = 140 + (i % 3) * 213;
    const char *kind = s.requested.type == profile::Type::Ramp ? "Rampa" : s.requested.type == profile::Type::Hold ? "Mantenimento" : "Cooldown passivo";
    snprintf(a, sizeof(a), "%u / %s / Target %.1f °C", i + 1, kind, double(s.requested.target)); w.text(44, y, a, 11, true);
    const char *state = s.completed ? "Completato" : s.started ? "Interrotto" : "Non iniziato";
    if (s.requested.type == profile::Type::Hold) snprintf(a, sizeof(a), "%s / richiesto %u min", state, unsigned(s.requested.minutes));
    else snprintf(a, sizeof(a), "%s / %.2f °C/min", state, double(s.requested.rate));
    w.text(44, y + 21, a, 9);
    duration(s.activeMs, a, sizeof(a)); w.pair(44, y + 43, "Tempo attivo", a);
    duration(s.pausedMs, a, sizeof(a)); w.pair(308, y + 43, "Pause", a);
    if (s.requested.type == profile::Type::Hold) {
      duration(s.holdInBandMs, a, sizeof(a)); duration(uint32_t(s.requested.minutes) * 60000, b, sizeof(b));
      char combined[150]; snprintf(combined, sizeof(combined), "%s / %s", a, b); w.pair(44, y + 81, "Hold in banda / richiesto", combined);
      if (s.stats.valid[0] && s.stats.valid[1]) snprintf(a, sizeof(a), "%.2f / %.2f °C", double(s.stats.high[0] - s.stats.low[0]), double(s.stats.high[1] - s.stats.low[1]));
      else snprintf(a, sizeof(a), "Non disponibile");
      w.pair(308, y + 81, "Escursione PT100 in hold", a);
    } else { w.pair(44, y + 81, "Hold in banda / richiesto", "Non applicabile"); w.pair(308, y + 81, "Escursione PT100 in hold", "Non applicabile"); }
    value(s.stats.trackingSamples ? s.stats.absoluteErrorSum / s.stats.trackingSamples : NAN, a, sizeof(a)); w.pair(44, y + 119, "Errore medio assoluto", a);
    value(s.stats.trackingSamples ? s.stats.maxOvershoot : NAN, a, sizeof(a)); w.pair(308, y + 119, "Superamento massimo", a);
    value(s.stats.trackingSamples ? s.stats.maxLag : NAN, a, sizeof(a)); w.pair(44, y + 157, "Ritardo massimo", a);
    if (s.stats.trackingSamples) snprintf(a, sizeof(a), "%.2f%% / %.2f%%", s.stats.dutySum / s.stats.trackingSamples, 100.0 * s.stats.saturatedSamples / s.stats.trackingSamples);
    else snprintf(a, sizeof(a), "Non disponibile");
    w.pair(308, y + 157, "PID medio / al limite", a); w.line(44, y + 189, 551, y + 189);
    if (yield) yield();
  }
  return w.finish();
}
} // namespace reportpdf
