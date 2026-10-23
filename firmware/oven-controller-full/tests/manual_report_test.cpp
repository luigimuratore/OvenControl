#include <cassert>
#include <cstring>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "../src/cycle_report.h"
#include "../src/report_pdf.h"
#include "../src/telegram_delivery.h"

int main(int argc, char **argv) {
  reports::Recorder r;
  r.beginManual(100); r.data.boot = 7; r.data.maxPower = 60;
  assert(r.active && reports::isManual(r.data) && r.data.stepCount == 0);
  assert(!strcmp(r.data.recipeName, "Controllo manuale"));
  r.observe(100, 24, 26, true, true, 25, 0);
  r.observe(1100, 49, 51, true, true, 50, 20);
  r.observe(2100, 69, 71, true, true, 75, 60); // Target changed within the same session.
  r.observe(3100, 100, 102, true, true, 60, 0, true); // Cooling: thermals only.
  r.observe(4100, NAN, 30, false, true, 30, 0, true);
  assert(r.finish(5100, reports::Outcome::Stopped, 0));
  const auto stopped = r.data;
  assert(stopped.startMs == 100 && stopped.endMs == 5100 && stopped.activeMs == 5000);
  assert(stopped.stats.samples == 5 && stopped.stats.invalidPairs == 1 && stopped.stats.trackingSamples == 3);
  assert(stopped.stats.maxOvershoot == 0 && stopped.stats.maxLag == 5 && stopped.stats.high[1] == 102);
  assert(!r.finish(5200, reports::Outcome::Stopped, 0)); // Repeated STOP cannot enqueue a new report.
  r.observe(6000, 400, 400, true, true, 400, 100);
  assert(r.data.endMs == stopped.endMs && r.data.stats.samples == stopped.stats.samples);
  r.beginManual(10000); r.data.boot = 7; r.data.maxPower = 60;
  r.observe(11000, 100, 101, true, true, 100, 10);
  assert(r.finish(12000, reports::Outcome::Fault, 0) && r.canContinue());
  const auto fault = r.data;
  assert(r.continueAfterFault(17000));
  assert(r.data.activeMs == 2000 && r.data.pausedMs == 5000 && r.data.pauseCount == 1);
  r.observe(18000, 100, 100, true, true, 100, 10);
  assert(r.finish(19000, reports::Outcome::Stopped, 0));
  assert(r.data.activeMs == 4000 && r.data.pausedMs == 5000 && r.data.startMs == fault.startMs);
  telegram::RecentReportCycles sent;
  sent.remember(fault.boot, fault.startMs, fault.endMs);
  assert(sent.contains(fault.boot, fault.startMs, fault.endMs));
  assert(!sent.contains(r.data.boot, r.data.startMs, r.data.endMs)); // New final report after explicit continuation.
  r.beginManual(UINT32_MAX - 99);
  assert(r.finish(100, reports::Outcome::Stopped, 0) && r.data.activeMs == 200);
  const profile::Step step{profile::Type::Hold, 50, 0, 1};
  r.begin(500, &step, 1);
  assert(!reports::isManual(r.data) && r.data.stepCount == 1 && r.data.stats.samples == 0);
  // Exercise the actual firmware PDF renderer with no artificial recipe steps.
  r.beginManual(0); r.data.boot = 7; r.data.startUtc = 1792742400;
  r.data.kp = 12; r.data.ki = .02f; r.data.kd = 50; r.data.ssr = true; r.data.windowMs = 5000; r.data.maxPower = 60;
  std::vector<reportpdf::Point> points;
  for (unsigned i = 0; i <= 180; ++i) {
    const float target = i <= 60 ? 25 + i * 25.f / 60 : i <= 90 ? 50 : i <= 150 ? 50 + (i - 90) * 25.f / 60 : 75 - (i - 150) * 1.5f;
    const float actual = target - .3f + .2f * std::sin(i / 8.f);
    r.observe(i * 10000, actual - .15f, actual + .15f, true, true, target, 10, i > 150);
    points.push_back({i * 10000, actual, target, 255, uint8_t(i == 0 ? 16 : i == 180 ? 32 : 0)});
  }
  assert(r.finish(1800000, reports::Outcome::Stopped, 0));
  r.data.endUtc = r.data.startUtc + 1800;
  std::strcpy(r.data.reason, "STOP manuale - dati dimostrativi per verifica del PDF.");
  std::vector<char> buffer(reportpdf::kCapacity);
  const size_t bytes = reportpdf::generate(buffer.data(), buffer.size(), r.data, points.data(), points.size(), false);
  assert(bytes > 1000 && bytes < buffer.size());
  const std::string pdf(buffer.data(), bytes);
  assert(pdf.find("/Count 2 /Kids") != std::string::npos);
  assert(pdf.find("CONTROLLO MANUALE FERMATO") != std::string::npos);
  assert(pdf.find("Fasi completate") == std::string::npos);
  char filename[128]; reportpdf::filename(r.data, filename, sizeof(filename));
  assert(!strncmp(filename, "Stopped_Controllo_manuale_", 24));
  if (argc > 1) { std::ofstream out(argv[1], std::ios::binary); out.write(pdf.data(), bytes); assert(out.good()); }
  std::cout << "Manual report: STOP idempotence, target changes, cooldown exclusion, invalid probes, alarm continuation, independent delivery identity and timer wrap passed\n";
}
