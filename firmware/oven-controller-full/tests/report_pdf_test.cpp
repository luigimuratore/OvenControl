#include <cassert>
#include <fstream>
#include <string>
#include <vector>
#include "../src/report_pdf.h"

int main(int argc, char **argv) {
  reports::Snapshot report;
  report.boot = 3; report.startMs = 0xffff0000; report.endMs = report.startMs + 43200000;
  report.startUtc = 1791543600; report.endUtc = report.startUtc + 43200;
  report.activeMs = 43080000; report.pausedMs = 120000; report.pauseCount = 1;
  report.stepCount = 12; report.completedSteps = 2; report.outcome = reports::Outcome::Fault;
  strcpy(report.recipeName, "Collaudo (carico) - ceramica è già calda");
  strcpy(report.reason, "PT100/MAX31865 in errore: uscita spenta. PT100 #2 Fault 0x04. Sovra/sottotensione sugli ingressi RTD; ciclo interrotto per allarme.");
  report.kp = 12; report.ki = .015; report.kd = 50; report.ssr = true; report.windowMs = 5000; report.maxPower = 30;
  std::vector<reportpdf::Point> curve;
  for (unsigned i = 0; i < 4321; ++i) {
    const float target = 30 + 370 * std::min(1.f, i / 2200.f);
    curve.push_back({report.startMs + i * 10000, i == 120 ? NAN : target - .7f, target});
    report.stats.observe(target - .9f, target - .5f, true, i != 120, true, target, 8.42f, 30);
  }
  for (unsigned i = 0; i < report.stepCount; ++i) {
    auto &s = report.steps[i]; s.requested.type = i % 3 == 0 ? profile::Type::Ramp : i % 3 == 1 ? profile::Type::Hold : profile::Type::Cooldown;
    s.requested.target = 400; s.requested.rate = 1; s.requested.minutes = 3;
    s.activeMs = 120000; s.stats = report.stats; s.holdInBandMs = 60000;
    s.started = i < 3; s.completed = i < 2;
  }
  std::vector<char> buffer(reportpdf::kCapacity);
  const size_t size = reportpdf::generate(buffer.data(), buffer.size(), report, curve.data(), curve.size(), true);
  assert(size > 200000 && size < buffer.size());
  const std::string pdf(buffer.data(), size);
  assert(pdf.find("/Count 6 /Kids") != std::string::npos);
  assert(pdf.find("CICLO IN ALLARME") != std::string::npos);
  assert(pdf.find("nan") == std::string::npos && pdf.find("inf") == std::string::npos);
  assert(!reportpdf::generate(buffer.data(), 1024, report, curve.data(), curve.size(), false));
  // Clock wrap, unsafe filename characters, and DST transition boundaries.
  char filename[128]; reportpdf::filename(report, filename, sizeof(filename));
  assert(strstr(filename, ".pdf") && !strchr(filename, '(') && !strchr(filename, '\n'));
  assert(strncmp(filename, "Error_", 6) == 0);
  report.outcome = reports::Outcome::Completed; reportpdf::filename(report, filename, sizeof(filename));
  assert(strncmp(filename, "Completed_", 10) == 0);
  report.outcome = reports::Outcome::Stopped; reportpdf::filename(report, filename, sizeof(filename));
  assert(strncmp(filename, "Stopped_", 8) == 0);
  report.outcome = reports::Outcome::Fault;
  assert(reportpdf::romeTime(1774745999) - 1774745999 == 3600); // 29 Mar 2026, 00:59:59 UTC
  assert(reportpdf::romeTime(1774746000) - 1774746000 == 7200);
  assert(reportpdf::romeTime(1792889999) - 1792889999 == 7200); // 25 Oct 2026, 00:59:59 UTC
  assert(reportpdf::romeTime(1792890000) - 1792890000 == 3600);
  if (argc > 1) {
    std::ofstream out(argv[1], std::ios::binary); out.write(pdf.data(), pdf.size()); assert(out.good());
  }
  report.endUtc = 0; reportpdf::filename(report, filename, sizeof(filename));
  assert(strstr(filename, "senza_orario_avvio-3_"));
  assert(strncmp(filename, "Error_", 6) == 0);
  assert(reportpdf::generate(buffer.data(), buffer.size(), report, nullptr, 0, true));
}
