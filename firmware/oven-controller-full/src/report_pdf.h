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
struct Point {
  uint32_t ms; float mean, target; uint8_t step, flags;
  Point(uint32_t at = 0, float actual = NAN, float reference = NAN, uint8_t phase = 255, uint8_t markers = 0)
    : ms(at), mean(actual), target(reference), step(phase), flags(markers) {}
};
struct StepRange { uint32_t start, duration; };
inline StepRange stepRange(const reports::Snapshot &r, unsigned index) {
  uint32_t start = r.startMs;
  for (unsigned i = 0; i < index; ++i) start += r.steps[i].activeMs + r.steps[i].pausedMs;
  return {start, r.steps[index].activeMs + r.steps[index].pausedMs};
}
inline const char *outcome(reports::Outcome value) {
  return value == reports::Outcome::Completed ? "CICLO COMPLETATO" :
         value == reports::Outcome::Fault ? "CICLO IN ALLARME" : "CICLO FERMATO";
}
inline const char *outcome(const reports::Snapshot &report) {
  return reports::isManual(report) ? (report.outcome == reports::Outcome::Fault ?
    "CONTROLLO MANUALE IN ALLARME" : "CONTROLLO MANUALE FERMATO") : outcome(report.outcome);
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
  size_t offsets[64]{};
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
  void object(unsigned id) { if (id >= 64) { good = false; return; } offsets[id] = used; put("%u 0 obj\n", id); }
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
  void panel(float x, float y, float width, float height, const char *fill = "1 1 1") {
    const float top = 842-y, bottom = top-height, right = x+width, radius = 7, bend = 3.87f;
    put("q %s rg 0.85 0.89 0.93 RG 0.5 w\n", fill);
    put("%.1f %.1f m %.1f %.1f l %.1f %.1f %.1f %.1f %.1f %.1f c\n", x+radius,top,right-radius,top,right-radius+bend,top,right,top-radius+bend,right,top-radius);
    put("%.1f %.1f l %.1f %.1f %.1f %.1f %.1f %.1f c\n", right,bottom+radius,right,bottom+radius-bend,right-radius+bend,bottom,right-radius,bottom);
    put("%.1f %.1f l %.1f %.1f %.1f %.1f %.1f %.1f c\n", x+radius,bottom,x+radius-bend,bottom,x,bottom+radius-bend,x,bottom+radius);
    put("%.1f %.1f l %.1f %.1f %.1f %.1f %.1f %.1f c h B Q\n", x,top-radius,x,top-radius+bend,x+radius-bend,top,x+radius,top);
  }
  void page(const char *title, const reports::Snapshot &r, unsigned total) {
    if (pages) endPage();
    streamId = 5 + pages * 2; ++pages;
    object(streamId); put("<< /Length "); lengthAt = used; put("0000000000 >>\nstream\n"); contentAt = used;
    put("0.957 0.969 0.980 rg 0 0 595 842 re f\n1 1 1 rg 0 732 595 110 re f\n");
    line(0,2,595,2,"0.22 0.48 0.66",4);
    text(44, 38, reports::isManual(r) ? "OVEN CONTROLLER / REPORT MANUALE" : "OVEN CONTROLLER / REPORT DI CICLO", 9, true);
    wrap(44, 76, title, 48, 18, false);
    line(44, 109, 551, 109);
    char footer[150]; snprintf(footer, sizeof(footer), "%s %u-%u-%u  |  Telegram  |  %u / %u",
      reports::isManual(r) ? "Sessione" : "Ciclo", unsigned(r.boot), unsigned(r.startMs), unsigned(r.endMs), pages, total);
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
inline void statsPairs(Writer &w, const reports::Stats &s, float y, bool meanOvershoot) {
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
  snprintf(a, sizeof(a), "%u campioni / coppie non valide: %u", unsigned(s.samples), unsigned(s.invalidPairs));
  w.pair(308, y, "Qualità delle letture", a);
  y += 69;
  value(s.trackingSamples ? s.absoluteErrorSum / s.trackingSamples : NAN, a, sizeof(a));
  w.pair(44, y, "Errore medio assoluto", a);
  value(s.trackingSamples ? s.maxOvershoot : NAN, a, sizeof(a)); w.pair(308, y, meanOvershoot ? "Superamento massimo media" : "Superamento massimo sonda", a);
  y += 43; value(s.trackingSamples ? s.maxLag : NAN, a, sizeof(a)); w.pair(44, y, "Ritardo massimo media", a);
  value(s.trackingSamples ? s.dutySum / s.trackingSamples : NAN, a, sizeof(a), "%");
  w.pair(308, y, "PID medio richiesto", a);
  y += 43; value(s.trackingSamples ? 100.0f * s.saturatedSamples / s.trackingSamples : NAN, a, sizeof(a), "%");
  w.pair(44, y, "Campioni al limite comando", a);
}
inline bool inChart(const Point &point, uint32_t start, uint32_t durationMs, int phase = -1) {
  return uint32_t(point.ms - start) <= durationMs &&
    (phase < 0 || point.step == 255 || point.step == phase);
}
inline void chart(Writer &w, const Point *points, size_t count, uint32_t start, uint32_t durationMs,
                  float top, float bottom, bool enabled, char *a, size_t size, void (*yield)(), int phase = -1) {
  const float left = 79, right = 543;
  float lo = INFINITY, hi = -INFINITY;
  size_t selected = 0, lastIndex = 0;
  for (size_t i = 0; enabled && i < count; ++i) if (inChart(points[i], start, durationMs, phase)) {
    ++selected; lastIndex = i;
    for (float v : {points[i].mean, points[i].target}) if (std::isfinite(v)) { lo = fminf(lo, v); hi = fmaxf(hi, v); }
  }
  if (!std::isfinite(lo)) { lo = 0; hi = 1; }
  lo = floorf(lo - 1); hi = ceilf(hi + 1);
  const float span = fmaxf(1000, durationMs);
  w.line(308, top - 22, 328, top - 22, "0.29 0.64 1", 2); w.text(334, top - 18, "Target", 9);
  w.line(427, top - 22, 447, top - 22, "0.21 0.66 0.51", 2); w.text(454, top - 18, "Media PT100", 9);
  for (unsigned tick = 0; tick <= 5; ++tick) {
    const float yy = bottom - (bottom - top) * tick / 5;
    w.line(left, yy, right, yy); snprintf(a, size, "%.1f", double(lo + (hi - lo) * tick / 5)); w.text(44, yy + 3, a, 8);
    const float xx = left + (right - left) * tick / 5;
    w.line(xx, top, xx, bottom); snprintf(a, size, "%.1f", double(span * tick / 300000)); w.text(xx - 8, bottom + 19, a, 8);
  }
  w.text(44, top - 17, "°C", 9); w.text(233, bottom + 42, "Tempo dall'inizio (min)", 9);
  const size_t stride = (selected + 799) / 800; // Bound vector size; keep gaps and endpoints.
  for (unsigned series = 0; series < 2; ++series) {
    bool previous = false, seen = false; float px = 0, py = 0; uint32_t last = 0; size_t ordinal = 0;
    for (size_t i = 0; enabled && i < count; ++i) {
      const uint32_t elapsed = uint32_t(points[i].ms - start);
      if (!inChart(points[i], start, durationMs, phase)) continue;
      const float v = series ? points[i].mean : points[i].target;
      if (!std::isfinite(v)) { previous = seen = false; ++ordinal; continue; }
      if ((seen && uint32_t(points[i].ms - last) > 25000) || (!series && (points[i].flags & 16))) previous = false;
      last = points[i].ms; seen = true;
      if (ordinal++ % (stride ? stride : 1) && i != lastIndex) continue;
      const float x = left + elapsed / span * (right - left), y = bottom - (v - lo) / (hi - lo) * (bottom - top);
      const char *color = series ? "0.21 0.66 0.51" : "0.29 0.64 1";
      if (previous) w.line(px, py, x, y, color, 1.1f);
      else w.line(x - .5f, y, x + .5f, y, color, 1.4f);
      previous = true; px = x; py = y;
      if (yield && !(i % 64)) yield();
    }
  }
  if (!selected) w.text(183, (top + bottom) / 2, enabled ? "Curva di questa fase non disponibile" : "Fase non iniziata: nessuna curva", 11);
}
inline size_t generate(char *buffer, size_t capacity, const reports::Snapshot &r,
                       const Point *points, size_t count, bool partial, void (*yield)() = nullptr) {
  if (!buffer || capacity < 1024 || r.stepCount > reports::kMaxSteps || (count && !points)) return 0;
  Writer w(buffer, capacity); char a[512], b[100]; const unsigned pages = 2 + r.stepCount;
  w.page(r.recipeName, r, pages);
  const char *resultFill = r.outcome == reports::Outcome::Completed ? "0.925 0.976 0.949" :
    r.outcome == reports::Outcome::Fault ? "1 0.937 0.937" : "1 0.969 0.886";
  w.panel(34, 121, 527, 65, resultFill);
  w.panel(34, 194, 527, 89, "0.925 0.957 0.992");
  w.panel(34, 296, 527, 417);
  w.panel(34, 721, 527, 61, "0.941 0.953 0.969");
  const bool manual = reports::isManual(r);
  w.text(44, 141, outcome(r), 12, true);
  w.wrap(44, 162, r.reason, 96, 9);
  date(r.startUtc, a, sizeof(a)); w.pair(44, 205, "Inizio / ora italiana", a);
  date(r.endUtc, a, sizeof(a)); w.pair(308, 205, "Fine / ora italiana", a);
  duration(uint32_t(r.endMs - r.startMs), a, sizeof(a)); w.pair(44, 248, "Durata totale, incluse pause", a);
  snprintf(a, sizeof(a), "%u / %u", r.completedSteps, r.stepCount);
  w.pair(308, 248, manual ? "Tipo di controllo" : "Fasi completate", manual ? "Manuale / target variabile" : a);
  w.text(44, 310, manual ? "ANDAMENTO DELLA SESSIONE MANUALE" : "ANDAMENTO COMPLETO DEL CICLO", 11, true);
  chart(w, points, count, r.startMs, uint32_t(r.endMs - r.startMs), 353, 609, true, a, sizeof(a), yield);
  snprintf(a, sizeof(a), "%u campioni. %s", unsigned(count), partial ? "Curva parziale: alcuni campioni non disponibili." : manual ? "Curva acquisita durante la sessione." : "Curva acquisita durante ciclo e pause.");
  w.wrap(44, 684, a, 96, 9);
  w.wrap(44, 741, manual ? "La curva include i cambi di target e il cooldown. Il riepilogo seguente mostra tempi, regolazione, temperature e precisione della sessione; non sono previsti step automatici." : "Le pagine seguenti separano tempi, regolazione, temperature e precisione. Ogni fase ha una pagina con il proprio grafico e scala adattata ai suoi dati.", 96, 9);
  w.page(manual ? "Riepilogo / controllo manuale" : "Riepilogo / dati del ciclo", r, pages);
  w.panel(34, 125, 527, 113, "0.925 0.957 0.992");
  w.panel(34, 247, 527, 147, "0.925 0.976 0.957");
  w.panel(34, 402, 527, 149, "0.941 0.953 0.984");
  w.panel(34, 560, 527, 180);
  w.text(44, 143, "TEMPI E REGOLAZIONE", 11, true);
  duration(r.activeMs, a, sizeof(a)); w.pair(44, 161, "Tempo attivo", a);
  duration(r.pausedMs, b, sizeof(b)); snprintf(a, sizeof(a), "%u / %s", unsigned(r.pauseCount), b); w.pair(308, 161, "Pause / durata", a);
  snprintf(a, sizeof(a), "Kp %.3f / Ki %.4f / Kd %.2f", double(r.kp), double(r.ki), double(r.kd)); w.pair(44, 204, "PID usato", a);
  snprintf(a, sizeof(a), "%s / %u s / limite %.1f%%", r.ssr ? "SSR" : "Relè", unsigned(r.windowMs / 1000), double(r.maxPower)); w.pair(308, 204, "Attuatore / finestra / limite", a);
  w.text(44, 265, "TEMPERATURE / PT100", 11, true);
  statsPairs(w, r.stats, 282, r.version >= 3);
  w.text(44, 420, "PRECISIONE DELLA MEDIA E COMANDO PID", 10, true);
  w.text(44, 580, "COME LEGGERE I DATI", 10, true);
  w.wrap(44, 604, manual ?
    "Errore, superamento e ritardo confrontano la media PT100 con il setpoint istantaneo. Questi indici e il PID medio usano solo campioni validi di regolazione attiva, escluse discesa, cooldown e attesa dopo allarme." : r.version >= 3 ?
    "Errore medio: distanza media dal target. Superamento: massimo della media PT100 sopra il target. Ritardo: massimo della media sotto il target. Questi indici e il PID medio usano solo rampa e mantenimento attivi, escluse pause e cooldown." :
    "Report precedente: il superamento usa la sonda più calda. Errore e ritardo usano la media. Gli indici e il comando medio escludono pause e cooldown.", 96, 9);
  if (manual) snprintf(a, sizeof(a), "Temperature e differenza sonde comprendono tutta la sessione acquisita. Target e durata sono decisi dall'operatore, senza conteggio hold. L'attesa dopo un allarme è conteggiata come pausa. Il comando non misura potenza o energia elettrica.");
  else snprintf(a, sizeof(a), "Il mantenimento conta entrambe le PT100 entro +/-%.0f °C. Temperature e differenza sonde comprendono le pause. Campioni non validi interrompono la curva; il comando non misura potenza o energia elettrica.", r.version == 1 ? 1.0 : double(profile::kHoldBand));
  w.wrap(44, 680, a, 96, 9);
  for (unsigned i = 0; i < r.stepCount; ++i) {
    const auto &s = r.steps[i]; const auto range = stepRange(r, i);
    const char *kind = s.requested.type == profile::Type::Ramp ? "Rampa" : s.requested.type == profile::Type::Hold ? "Mantenimento" : "Cooldown";
    snprintf(a, sizeof(a), "Fase %u / %s / %.1f °C", i + 1, kind, double(s.requested.target)); w.page(a, r, pages);
    const char *state = s.completed ? "Completata" : s.started ? "Interrotta" : "Non iniziata";
    if (s.requested.type == profile::Type::Hold) snprintf(a, sizeof(a), "%s / richiesti %u min in banda", state, unsigned(s.requested.minutes));
    else snprintf(a, sizeof(a), "%s / %.2f °C/min", state, double(s.requested.rate));
    w.panel(34, 124, 527, 114, "0.925 0.957 0.992");
    w.panel(34, 251, 527, 297);
    w.panel(34, 552, 527, 113, "0.941 0.953 0.984");
    w.panel(34, 675, 527, 108, "0.925 0.976 0.957");
    w.text(44, 143, a, 10, true);
    duration(s.activeMs, a, sizeof(a)); w.pair(44, 164, "Tempo attivo", a);
    duration(s.pausedMs, a, sizeof(a)); w.pair(308, 164, "Tempo in pausa", a);
    if (s.requested.type == profile::Type::Hold) {
      duration(s.holdInBandMs, a, sizeof(a)); duration(uint32_t(s.requested.minutes) * 60000, b, sizeof(b));
      char combined[150]; snprintf(combined, sizeof(combined), "%s / %s", a, b); w.pair(44, 207, "Mantenimento in banda / richiesto", combined);
    } else w.pair(44, 207, "Mantenimento in banda", "Non applicabile");
    snprintf(a, sizeof(a), "%.1f °C", double(s.requested.target)); w.pair(308, 207, "Target finale della fase", a);
    w.text(44, 269, "GRAFICO DELLA FASE / TARGET E MEDIA", 11, true);
    chart(w, points, count, range.start, range.duration, 311, 489, s.started, a, sizeof(a), yield, i);
    w.text(44, 564, "PRECISIONE E REGOLAZIONE / FASE ATTIVA", 10, true);
    value(s.stats.trackingSamples ? s.stats.absoluteErrorSum / s.stats.trackingSamples : NAN, a, sizeof(a)); w.pair(44, 590, "Errore medio assoluto", a);
    value(s.stats.trackingSamples ? s.stats.maxOvershoot : NAN, a, sizeof(a)); w.pair(308, 590, r.version >= 3 ? "Superamento massimo media" : "Superamento massimo sonda", a);
    value(s.stats.trackingSamples ? s.stats.maxLag : NAN, a, sizeof(a)); w.pair(44, 633, "Ritardo massimo della media", a);
    if (s.stats.trackingSamples) snprintf(a, sizeof(a), "%.2f%% / %.2f%%", s.stats.dutySum / s.stats.trackingSamples, 100.0 * s.stats.saturatedSamples / s.stats.trackingSamples);
    else snprintf(a, sizeof(a), "Non disponibile");
    w.pair(308, 633, "PID medio / campioni al limite", a);
    w.text(44, 686, "TEMPERATURE / FASE E SUE PAUSE", 10, true);
    for (unsigned j = 0; j < 2; ++j) {
      snprintf(a, sizeof(a), "PT100 #%u / minimo - massimo", j + 1);
      if (s.stats.valid[j]) snprintf(b, sizeof(b), "%.2f - %.2f °C", double(s.stats.low[j]), double(s.stats.high[j]));
      else snprintf(b, sizeof(b), "Non disponibile");
      w.pair(44 + j * 264, 712, a, b);
    }
    if (s.requested.type == profile::Type::Hold) {
      if (s.stats.valid[0] && s.stats.valid[1]) snprintf(a, sizeof(a), "%.2f / %.2f °C", double(s.stats.high[0]-s.stats.low[0]), double(s.stats.high[1]-s.stats.low[1]));
      else snprintf(a, sizeof(a), "Non disponibile");
      w.pair(44, 756, "Escursione in mantenimento / PT100 #1 e #2", a);
    }
    if (partial) w.text(44, 791, "Storico parziale: i dati della fase potrebbero non essere tutti disponibili.", 8);
    if (yield) yield();
  }
  return w.finish();
}
} // namespace reportpdf
