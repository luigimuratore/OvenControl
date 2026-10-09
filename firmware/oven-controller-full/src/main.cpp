#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_MAX31865.h>
#include <esp_system.h>
#include <esp_sntp.h>
#include <nvs.h>
#include <cmath>
#include <ctime>
#include "profile.h"
#include "cycle_report.h"
#include "report_curve.h"
#include "output_supervisor.h"
#include "log_storage.h"
#include "sensor_recovery.h"
#include "thermal_limits.h"
#include "hardware_config.h"
#include "web_assets.h"
#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
#define WORKSHOP_WIFI_SSID ""
#define WORKSHOP_WIFI_PASSWORD ""
#define WORKSHOP_NTP_SERVER "pool.ntp.org"
#endif
#if __has_include("ota_config.h")
#include "ota_config.h"
#endif
#ifndef OVEN_OTA_PASSWORD
#define OVEN_OTA_PASSWORD ""
#endif
// ESP32-S3 N16R8; GPIO35/36 are occupied by PSRAM on this module.
constexpr int CS1 = PIN_CS1, CS2 = PIN_CS2;
constexpr int HEAT = PIN_RELAY, RED = PIN_RED, GREEN = PIN_GREEN;
constexpr float MAX_DELTA = 10.0f;
constexpr float MAX_TARGET_OVERSHOOT = 8.0f;
constexpr uint32_t SAMPLE_MS = 500, HISTORY_MS = 10000;
constexpr uint32_t CHECKPOINT_MS = 30000, MAX_CYCLE_MS = 12UL * 3600000UL;
constexpr size_t MAX_RECIPES = 8, MAX_STEPS = 12, HISTORY_CAP = 4320, HISTORY_PAGE = 180;
constexpr size_t EVENT_CAP = 128, PERSIST_CAP = 40;
constexpr char AP_SSID[] = "OvenController-Full";
constexpr char AP_PASSWORD[] = "oven12345";
constexpr char MDNS_NAME[] = "oven-full";
constexpr uint16_t OTA_PORT = 3232;
constexpr size_t TEST_HISTORY_CAP = 3600, TEST_HISTORY_PAGE = 300;

Adafruit_MAX31865 rtd1(CS1), rtd2(CS2);
WebServer server(80);
Preferences prefs;
bool prefsReady = false, eventWriteFaultLogged = false;

struct LogStore {
  static String key(size_t slot) { return "log" + String(slot); }
  bool freeEntries(size_t &free) {
    nvs_stats_t stats{};
    if (!prefsReady || nvs_get_stats(nullptr, &stats) != ESP_OK) return false;
    free = stats.free_entries; return true;
  }
  bool has(size_t slot) { return prefs.isKey(key(slot).c_str()); }
  bool remove(size_t slot) { return prefs.remove(key(slot).c_str()); }
} logStore;

struct Sensor { float c = NAN, ohms = NAN; uint16_t raw = 0; uint8_t fault = 0; bool valid = false, recoveryFailed = false; };
struct PidGains { float kp, ki, kd; };
// Default for initial low-temperature commissioning; select the actual actuator explicitly.
struct ControlSettings { uint32_t windowMs = 60000; float maxPower = 30; bool ssr = false; };
ControlSettings controlSettings;
OutputSupervisor outputs;
uint32_t ownerToken = 0;
bool testActiveSaved = false, testInterrupted = false, otaEnabled = false;
bool outputServerReady = false, outputAlarm = false;
portMUX_TYPE heaterMux = portMUX_INITIALIZER_UNLOCKED;
struct HeaterNotice { bool on, red, green; heater::Fault fault; TestEvent event; TestMode previous; };
QueueHandle_t heaterNotices;
profile::HoldTimer holdTimer;
struct Recipe { String id, name; profile::Step steps[MAX_STEPS]; uint8_t count = 0; };
struct Event { uint32_t seq, at; uint64_t utc; String level, message; bool saved; };
struct Sample { uint32_t seq, ms; float t1, t2, setpoint, duty; uint8_t flags, step; };
enum class Phase : uint8_t { Idle, Running, Paused, Complete, Fault, Interrupted };
constexpr uint8_t HISTORY_START = 0x10, HISTORY_STOP = 0x20;
constexpr uint8_t HISTORY_COMPLETE = 0x40, HISTORY_FAULT = 0x80;

Sensor sensors[2];
probes::Recovery sensorRecovery[2];
Recipe recipes[MAX_RECIPES], activeRecipe;
size_t recipeCount = 0;
Phase phase = Phase::Idle;
bool relayOn = false, apReady = false, ntpStarted = false, ntpSynced = false;
bool mdnsReady = false, stationWasConnected = false, sensorFaultLogged = false;
bool hadInterrupted = false, checkpointWriteFaultLogged = false;
struct TestSample { uint32_t seq, ms; float t1, t2; uint16_t raw1, raw2; uint8_t fault1, fault2, flags; };
TestSample *testHistory = nullptr;
size_t testHistoryCapacity = 0;
size_t testHistoryHead = 0, testHistoryCount = 0;
uint32_t testHistorySeq = 0, lastTestHistoryAt = 0;
uint32_t testRunSeq = 0, testStartedAtMs = 0;
float chipTemperatureC = NAN;
uint32_t lastChipTemperatureAt = 0;
String stationIp, faultText, resetReason, lastCycleError;
float setpointC = NAN, duty = 0, integral = 0, derivative = 0, previousMean = NAN;
float kp = 12.0f, ki = 0.015f, kd = 50.0f;
float stepStartC = NAN;
uint8_t stepIndex = 0;
uint32_t stepStarted = 0, cycleStarted = 0, pausedAt = 0, holdInBandMs = 0;
uint32_t previousControlAt = 0, windowStarted = 0, lastSampleAt = 0, lastHistoryAt = 0;
uint32_t lastNetworkCheck = 0, lastPidLog = 0, lastLagLog = 0, checkpointAt = 0;
uint32_t sampleAtMs = 0, bootCount = 0, eventSeq = 0, persistedSeq = 0;
uint64_t previousCheckpoint = 0, outageUpperBoundSec = 0;
struct InterruptionRecord { uint32_t resetReason = 0; uint64_t checkpointUtc = 0, rebootUtc = 0; };
InterruptionRecord lastInterruption;
bool interruptionRecorded = false;
Event events[EVENT_CAP]; size_t eventHead = 0, eventCount = 0;
reports::Recorder cycleReport;
reports::Snapshot lastReport;
bool reportAvailable = false, reportSaved = false;
Sample history[HISTORY_CAP]; size_t historyHead = 0, historyCount = 0; uint32_t historySeq = 0;
// Freeze the last cycle curve separately from the rotating dashboard history.
Sample *reportHistory = nullptr;
size_t reportHistoryCount = 0;
bool reportHistoryPartial = false;

uint64_t utcNow() {
  time_t now = time(nullptr);
  return ntpSynced && now >= 1700000000 ? uint64_t(now) : 0;
}
bool persistEvent(const Event &event) {
  char epoch[24];
  snprintf(epoch, sizeof(epoch), "%llu", (unsigned long long)event.utc);
  String line = String(epoch) + "|" + event.level + "|" + event.message;
  uint32_t next = persistedSeq + 1;
  String key = LogStore::key(next % PERSIST_CAP);
  // Remove only the log being replaced, so NVS need not hold both copies.
  bool saved = prefsReady && (!prefs.isKey(key.c_str()) || prefs.remove(key.c_str())) &&
               logs::makeRoom(logStore, persistedSeq, PERSIST_CAP,
                              logs::kReserveEntries + logs::stringEntries(line.length()) + 1) &&
               prefs.putString(key.c_str(), line) == line.length();
  if (saved && prefs.putUInt("logseq", next) != sizeof(uint32_t)) {
    prefs.remove(key.c_str()); saved = false;
  }
  if (saved) persistedSeq = next;
  else if (!eventWriteFaultLogged)
    Serial.println("[ATTENZIONE] Log non salvato in NVS: evento disponibile solo in RAM");
  eventWriteFaultLogged = !saved;
  return saved;
}
void logEvent(const String &level, const String &message, bool save = true) {
  Event event{++eventSeq, millis(), utcNow(), level, message, false};
  event.saved = save && persistEvent(event);
  events[eventHead] = event; eventHead = (eventHead + 1) % EVENT_CAP;
  if (eventCount < EVENT_CAP) ++eventCount;
  Serial.printf("[%s] %s\n", level.c_str(), message.c_str());
}
void loadEvents() {
  if (!prefsReady) return;
  persistedSeq = prefs.getUInt("logseq", 0);
  size_t count = persistedSeq < PERSIST_CAP ? persistedSeq : PERSIST_CAP;
  for (size_t i = 0; i < count; ++i) {
    uint32_t n = persistedSeq - uint32_t(count) + 1 + uint32_t(i);
    String key = LogStore::key(n % PERSIST_CAP);
    if (!prefs.isKey(key.c_str())) continue;
    String line = prefs.getString(key.c_str(), "");
    int a = line.indexOf('|'), b = line.indexOf('|', a + 1);
    if (a < 0 || b < 0) continue;
    events[eventHead] = {++eventSeq, 0, strtoull(line.substring(0, a).c_str(), nullptr, 10),
                         line.substring(a + 1, b), line.substring(b + 1), true};
    eventHead = (eventHead + 1) % EVENT_CAP;
    if (eventCount < EVENT_CAP) ++eventCount;
  }
}
OutputSupervisor outputSnapshot() {
  portENTER_CRITICAL(&heaterMux); auto result = outputs; portEXIT_CRITICAL(&heaterMux);
  return result;
}
bool heaterOn() { return outputSnapshot().relay; }
heater::Fault heaterFault() { return outputSnapshot().guard.fault; }
bool diagnosticMode() { return outputSnapshot().diagnostic; }
bool cycleActive() { return phase == Phase::Running || phase == Phase::Paused; }
void writeOutputs() {
  // Caller holds heaterMux; only this supervisor owns the GPIOs.
  digitalWrite(HEAT, outputs.relay == RELAY_ACTIVE_HIGH ? HIGH : LOW);
  digitalWrite(RED, outputs.red == LED_ACTIVE_HIGH ? HIGH : LOW);
  digitalWrite(GREEN, outputs.green == LED_ACTIVE_HIGH ? HIGH : LOW);
}
void heaterTask(void *) {
  bool previousOn = false, previousRed = false, previousGreen = false;
  heater::Fault previousFault = heater::Fault::None;
  for (;;) {
    portENTER_CRITICAL(&heaterMux);
    const TestMode previousMode = outputs.test.mode;
    const auto event = outputs.tick(millis(), outputServerReady, outputAlarm);
    if (outputs.test.mode == TestMode::Idle) ownerToken = 0;
    writeOutputs();
    HeaterNotice notice{outputs.relay, outputs.red, outputs.green, outputs.guard.fault, event, previousMode};
    portEXIT_CRITICAL(&heaterMux);
    if (notice.on != previousOn || notice.red != previousRed || notice.green != previousGreen ||
        notice.fault != previousFault || event != TestEvent::None) {
      xQueueSend(heaterNotices, &notice, 0);
      previousOn = notice.on; previousRed = notice.red; previousGreen = notice.green; previousFault = notice.fault;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
void inhibitHeater() {
  portENTER_CRITICAL(&heaterMux);
  outputs.guard.inhibit();
  // Sensor/PID sampling cannot cancel a diagnostic test.
  if (!outputs.diagnostic) { outputs.relay = outputs.red = false; writeOutputs(); }
  portEXIT_CRITICAL(&heaterMux);
  relayOn = heaterOn();
}
void stopTests(const String &reason, bool leave = false) {
  const bool wasRunning = outputSnapshot().test.mode != TestMode::Idle;
  portENTER_CRITICAL(&heaterMux); outputs.stopTest(leave); ownerToken = 0; writeOutputs(); portEXIT_CRITICAL(&heaterMux);
  if (testActiveSaved) prefs.putBool("testActive", false);
  testActiveSaved = false;
  if (wasRunning || leave) logEvent("TEST", reason);
}
const char *testModeName(TestMode mode) {
  switch (mode) {
    case TestMode::RelayPulse: return "Rele temporizzato";
    case TestMode::RelaySequence: return "Rele: 3 impulsi";
    case TestMode::LedGreen: return "LED verde";
    case TestMode::LedRed: return "LED rosso";
    case TestMode::LedBoth: return "Entrambi i LED";
    case TestMode::LedOff: return "LED spenti";
    case TestMode::LedSequence: return "Sequenza LED";
    default: return "Lettura sonde";
  }
}
void resetPid() {
  duty = integral = derivative = 0;
  previousMean = NAN; previousControlAt = 0; windowStarted = millis();
  inhibitHeater();
}
void recordHistory(bool force = false, uint8_t eventFlag = 0);
void finishCycleReport(Phase next, const String &reason);
void stopCycle(Phase next, const String &reason) {
  const float finalReference = setpointC;
  inhibitHeater(); duty = 0;
  if (phase == Phase::Running || phase == Phase::Paused) {
    if (next == Phase::Fault) {
      lastCycleError = reason.substring(0, 240);
      // Best effort: the report and log also record the fault if NVS is full.
      prefs.putString("cycleError", lastCycleError);
    }
    phase = next;
    setpointC = NAN;
    prefs.putBool("active", false);
    finishCycleReport(next, reason);
    if (reportHistory && reportAvailable) {
      reportHistoryCount = reports::copyCurve(history, HISTORY_CAP, historyHead, historyCount,
        lastReport.startMs, lastReport.endMs, reportHistory, HISTORY_CAP, reportHistoryPartial);
      const uint8_t flags = (sensors[0].valid ? 1 : 0) | (sensors[1].valid ? 2 : 0) |
                           (std::isfinite(finalReference) ? 8 : 0) |
                           (next == Phase::Complete ? HISTORY_COMPLETE : next == Phase::Fault ? HISTORY_FAULT : HISTORY_STOP);
      reportHistory[reportHistoryCount++] = {historySeq + 1, lastReport.endMs, sensors[0].c, sensors[1].c,
                                           finalReference, 0, flags, stepIndex};
    }
    logEvent(next == Phase::Fault ? "ALLARME" : "CICLO", reason);
    recordHistory(true, next == Phase::Complete ? HISTORY_COMPLETE :
                        next == Phase::Fault ? HISTORY_FAULT : HISTORY_STOP);
  } else if (next == Phase::Fault && phase != Phase::Fault) {
    phase = Phase::Fault; logEvent("ALLARME", reason);
  }
  if (next == Phase::Fault) faultText = reason;
}
void trip(const String &reason) { stopCycle(Phase::Fault, reason); }
void drainHeaterNotices() {
  HeaterNotice notice;
  while (xQueueReceive(heaterNotices, &notice, 0) == pdTRUE) {
    if (notice.event == TestEvent::Completed) logEvent("TEST", String(testModeName(notice.previous)) + ": terminato automaticamente");
    if (notice.event == TestEvent::ConnectionLost) logEvent("STOP", "Heartbeat assente da 2,5 s: test fermato");
    logEvent("GPIO", String("Comando uscita ") + (notice.on ? "ON" : "OFF") +
             "; rosso " + (notice.red ? "ON" : "OFF") + "; verde " + (notice.green ? "ON" : "OFF"), false);
  }
  if (testActiveSaved && outputSnapshot().test.mode == TestMode::Idle) {
    prefs.putBool("testActive", false); testActiveSaved = false;
  }
  relayOn = heaterOn();
  auto fault = heaterFault();
  if (fault != heater::Fault::None && phase != Phase::Fault) {
    trip(fault == heater::Fault::StaleControl ? "Controllo/letture assenti da 3 s: supervisore ha spento l'uscita" :
         fault == heater::Fault::CycleTimeout ? "Durata massima 12 ore (incluse pause): uscita spenta" :
         "Piano di controllo non valido: uscita spenta");
  }
}
String sensorMessage(const Sensor &sensor);
void readSensorConversion(Adafruit_MAX31865 &rtd, Sensor &sensor) {
  sensor.raw = rtd.readRTD();
  sensor.fault = rtd.readFault();
  rtd.enableBias(false);
  float reference = &sensor == &sensors[0] ? RREF_1 : RREF_2;
  sensor.ohms = sensor.raw * reference / 32768.0f;
  sensor.c = rtd.calculateTemperature(sensor.raw, RNOMINAL, reference);
  sensor.valid = sensor.fault == 0 && sensor.raw > 0 && sensor.raw < 32767 &&
                 thermal::validReading(sensor.c, diagnosticMode());
  // Switch off before logging to flash or waiting for the other probe's conversion.
  if (!sensor.valid && cycleActive()) inhibitHeater();
}
void readSensor(Adafruit_MAX31865 &rtd, Sensor &sensor) {
  const bool wasValid = sensor.valid; const uint8_t previousFault = sensor.fault;
  const size_t index = &sensor == &sensors[0] ? 0 : 1;
  sensor.recoveryFailed = false;
  readSensorConversion(rtd, sensor);
  const uint8_t initialFault = sensor.fault;
  const uint16_t initialRaw = sensor.raw;
  const auto recovery = sensorRecovery[index].verify(initialFault, millis(), cycleActive(),
    [] {
      inhibitHeater();
      // A fault/recheck interval must not count toward an in-band hold.
      holdTimer.update(millis(), false, false);
    },
    [&] {
      readSensorConversion(rtd, sensor);
      if (!sensor.valid || !thermal::belowHardLimit(sensor.c)) return false;
      const auto &other = sensors[1 - index];
      if (other.valid && fabsf(sensor.c - other.c) > MAX_DELTA) return false;
      // Do not discard an unsafe but otherwise valid confirmation by reading again.
      return phase != Phase::Running || stepIndex >= activeRecipe.count ||
             activeRecipe.steps[stepIndex].type == profile::Type::Cooldown ||
             !std::isfinite(setpointC) || sensor.c < setpointC + MAX_TARGET_OVERSHOOT;
    });
  sensor.recoveryFailed = recovery == probes::RecoveryResult::Failed;
  // Preserve rejection even if the final conversion has no hardware fault bit:
  // a failed safety check must not become acceptable after reading the other probe.
  if (sensor.recoveryFailed) sensor.valid = false;
  if (recovery != probes::RecoveryResult::NotAttempted)
    logEvent("SONDA", "PT100 #" + String(index + 1) + ": Fault 0x" + String(initialFault, HEX) +
      " · RAW " + String(initialRaw) +
      (recovery == probes::RecoveryResult::Confirmed ?
       " · transitorio recuperato con due nuove conversioni valide; uscita spenta durante verifica" :
       " · verifica non confermata; arresto e allarme"));
  if (sensor.valid != wasValid || sensor.fault != previousFault)
    logEvent(sensor.valid ? "PT100" : "SONDA", String(&sensor == &sensors[0] ? "PT100 #1: " : "PT100 #2: ") + sensorMessage(sensor) +
      " · Fault 0x" + String(sensor.fault, HEX) + " · RAW " + String(sensor.raw) + " · " + String(sensor.ohms, 2) + " ohm");
}
String sensorMessage(const Sensor &sensor) {
  if (sensor.valid) return "Lettura valida";
  String text;
  if (sensor.recoveryFailed) text += "Verifica del fault 0x04 non confermata entro i limiti di sicurezza; ";
  if (sensor.fault & MAX31865_FAULT_HIGHTHRESH) text += "RTD oltre soglia alta; ";
  if (sensor.fault & MAX31865_FAULT_LOWTHRESH) text += "RTD sotto soglia bassa; ";
  if (sensor.fault & MAX31865_FAULT_REFINLOW) text += "REFIN- > 85% VBIAS; ";
  if (sensor.fault & MAX31865_FAULT_REFINHIGH) text += "REFIN- < 85% VBIAS / FORCE- aperto; ";
  if (sensor.fault & MAX31865_FAULT_RTDINLOW) text += "RTDIN- basso / circuito aperto; ";
  if (sensor.fault & MAX31865_FAULT_OVUV) text += "Sovra/sottotensione sugli ingressi RTD; ";
  return text.length() ? text : "Lettura fuori intervallo o SPI/sonda assente";
}
float meanC() { return (sensors[0].c + sensors[1].c) * 0.5f; }
float hottestC() { return fmaxf(sensors[0].c, sensors[1].c); }
bool sensorsHealthy() {
  return sensors[0].valid && sensors[1].valid && thermal::belowHardLimit(hottestC()) &&
         fabsf(sensors[0].c - sensors[1].c) <= MAX_DELTA;
}
const char *typeName(profile::Type type) {
  return type == profile::Type::Ramp ? "ramp" : type == profile::Type::Hold ? "hold" : "cooldown";
}
const char *phaseName() {
  switch (phase) {
    case Phase::Running: return "running";
    case Phase::Paused: return "paused";
    case Phase::Complete: return "complete";
    case Phase::Fault: return "fault";
    case Phase::Interrupted: return "interrupted";
    default: return "idle";
  }
}
void beginStep() {
  if (stepIndex >= activeRecipe.count) {
    stopCycle(Phase::Complete, "Ciclo finito: tutti gli step completati"); return;
  }
  stepStarted = millis();
  const auto &step = activeRecipe.steps[stepIndex];
  stepStartC = profile::startTemperature(step, meanC(), stepIndex > 0 ? setpointC : NAN);
  holdInBandMs = 0;
  holdTimer.reset(stepStarted);
  resetPid();
  setpointC = profile::setpoint(step, stepStartC, 0);
  logEvent("CICLO", "Step " + String(stepIndex + 1) + "/" + String(activeRecipe.count) +
          " " + String(typeName(step.type)) + " verso " + String(step.target, 1) + " °C" +
          (step.type == profile::Type::Hold ? "; " + String(step.minutes) + " min in banda" :
           "; " + String(step.rate, 2) + " °C/min"));
}
void advanceStep() { cycleReport.advance(millis(), holdInBandMs); ++stepIndex; beginStep(); }
void applyWindow() {
  heater::Plan plan;
  plan.cycleActive = phase == Phase::Running || phase == Phase::Paused;
  plan.heatingAllowed = phase == Phase::Running && stepIndex < activeRecipe.count &&
                        activeRecipe.steps[stepIndex].type != profile::Type::Cooldown && std::isfinite(setpointC);
  plan.healthy = sensorsHealthy(); plan.sampleAt = sampleAtMs;
  plan.cycleAt = cycleStarted; plan.windowAt = windowStarted;
  plan.windowMs = controlSettings.windowMs; plan.duty = duty;
  portENTER_CRITICAL(&heaterMux); outputs.publish(plan); portEXIT_CRITICAL(&heaterMux);
}
void controlPid(float actual, uint32_t now) {
  float dt = previousControlAt ? float(uint32_t(now - previousControlAt)) / 1000.0f : 0.5f;
  dt = constrain(dt, 0.1f, 2.0f);
  // Conservative starting gains, to be tuned on the actual oven with a low-temperature recipe.
  float error = setpointC - actual;
  float rawDerivative = std::isfinite(previousMean) ? -(actual - previousMean) / dt : 0;
  derivative = 0.8f * derivative + 0.2f * rawDerivative;
  float candidate = constrain(integral + ki * error * dt, 0.0f, controlSettings.maxPower);
  float raw = kp * error + candidate + kd * derivative;
  if (!((raw >= controlSettings.maxPower && error > 0) || (raw <= 0 && error < 0))) integral = candidate;
  duty = constrain(kp * error + integral + kd * derivative, 0.0f, controlSettings.maxPower);
  if (hottestC() >= setpointC + 1.0f) { duty = 0; integral = 0; }
  previousMean = actual; previousControlAt = now;
  applyWindow();
  if (uint32_t(now - lastPidLog) >= 30000) {
    lastPidLog = now;
    logEvent("PID", "Media " + String(actual, 1) + " °C; setpoint " + String(setpointC, 1) +
             " °C; uscita " + String(duty, 0) + "%", false);
  }
}
void sampleAndControl() {
  readSensor(rtd1, sensors[0]); readSensor(rtd2, sensors[1]);
  uint32_t now = millis(); sampleAtMs = now;
  if (cycleActive() && stepIndex < activeRecipe.count) {
    const float reference = phase == Phase::Running ?
      profile::setpoint(activeRecipe.steps[stepIndex], stepStartC, uint32_t(now - stepStarted)) : setpointC;
    cycleReport.observe(now, sensors[0].c, sensors[1].c, sensors[0].valid, sensors[1].valid, reference, duty);
  }
  if (!sensors[0].valid || !sensors[1].valid) {
    if (!sensorFaultLogged) { logEvent("ATTENZIONE", "Una o entrambe le PT100 non sono leggibili"); sensorFaultLogged = true; }
    if (phase == Phase::Running || phase == Phase::Paused) {
      // Keep the failing sample in the alarm/report, even if the next sample recovers.
      String reason = "PT100/MAX31865 in errore: uscita spenta";
      for (size_t i = 0; i < 2; ++i) if (!sensors[i].valid)
        reason += " · PT100 #" + String(i + 1) + " Fault 0x" + String(sensors[i].fault, HEX) + ", RAW " + String(sensors[i].raw);
      for (size_t i = 0; i < 2; ++i) if (!sensors[i].valid)
        reason += " · #" + String(i + 1) + ": " + sensorMessage(sensors[i]);
      trip(reason);
    }
    else inhibitHeater();
    return;
  }
  if (sensorFaultLogged) { logEvent("INFO", "Entrambe le PT100 leggibili"); sensorFaultLogged = false; }
  if (diagnosticMode()) { holdTimer.update(now, false, false); return; }
  if (!thermal::belowHardLimit(hottestC())) {
    trip("Temperatura >= " + String(thermal::kHardLimit, 0) + " °C: uscita spenta"); return;
  }
  if (fabsf(sensors[0].c - sensors[1].c) > MAX_DELTA) {
    trip("Differenza PT100 > 10 °C: uscita spenta"); return;
  }
  if ((phase == Phase::Running || phase == Phase::Paused) && uint32_t(now - cycleStarted) >= MAX_CYCLE_MS) {
    trip("Tempo massimo ciclo 12 ore, incluse pause, superato"); return;
  }
  if (phase != Phase::Running) { holdTimer.update(now, false, false); inhibitHeater(); return; }
  const auto &step = activeRecipe.steps[stepIndex];
  float actual = meanC();
  uint32_t elapsed = uint32_t(now - stepStarted);
  setpointC = profile::setpoint(step, stepStartC, elapsed);
  if (step.type != profile::Type::Cooldown && hottestC() >= setpointC + MAX_TARGET_OVERSHOOT) {
    trip("Sovratemperatura rispetto al setpoint (+8 °C): uscita spenta"); return;
  }
  if (step.type == profile::Type::Cooldown) {
    duty = 0; inhibitHeater();
    if (profile::cooldownDone(step, stepStartC, elapsed, hottestC())) advanceStep();
    return;
  }
  controlPid(actual, now);
  if (step.type == profile::Type::Ramp) {
    if (profile::rampDone(step, stepStartC, elapsed, fminf(sensors[0].c, sensors[1].c))) {
      advanceStep(); return;
    }
    if (setpointC - actual > 5 && uint32_t(now - lastLagLog) > 300000) {
      lastLagLog = now;
      logEvent("ATTENZIONE", "Il forno segue la rampa con oltre 5 °C di ritardo");
    }
  } else {
    holdTimer.update(now, profile::inBand(sensors[0].c, sensors[1].c, step.target));
    holdInBandMs = holdTimer.elapsedMs;
    if (holdInBandMs >= uint32_t(step.minutes) * 60000UL) advanceStep();
  }
}
void recordHistory(bool force, uint8_t eventFlag) {
  uint32_t now = millis();
  relayOn = heaterOn();
  if (!force && uint32_t(now - lastHistoryAt) < HISTORY_MS) return;
  lastHistoryAt = now;
  uint8_t flags = (sensors[0].valid ? 1 : 0) | (sensors[1].valid ? 2 : 0) |
                  (relayOn ? 4 : 0) | (std::isfinite(setpointC) ? 8 : 0) | eventFlag;
  history[historyHead] = {++historySeq, now, sensors[0].valid ? sensors[0].c : 0,
                          sensors[1].valid ? sensors[1].c : 0,
                          std::isfinite(setpointC) ? setpointC : 0, duty, flags, stepIndex};
  historyHead = (historyHead + 1) % HISTORY_CAP;
  if (historyCount < HISTORY_CAP) ++historyCount;
}

void sendJson(int code, const JsonDocument &doc) {
  String body; serializeJson(doc, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", body);
}
void error(int code, const String &message) { JsonDocument doc; doc["error"] = message; sendJson(code, doc); }
void recipeToJson(JsonObject item, const Recipe &recipe) {
  item["id"] = recipe.id; item["name"] = recipe.name;
  JsonArray steps = item["steps"].to<JsonArray>();
  for (size_t i = 0; i < recipe.count; ++i) {
    JsonObject step = steps.add<JsonObject>();
    step["type"] = typeName(recipe.steps[i].type);
    step["target"] = recipe.steps[i].target;
    if (recipe.steps[i].type == profile::Type::Hold) step["duration"] = recipe.steps[i].minutes;
    else step["rate"] = recipe.steps[i].rate;
  }
}
void finishCycleReport(Phase next, const String &reason) {
  const auto outcome = next == Phase::Complete ? reports::Outcome::Completed :
                       next == Phase::Fault ? reports::Outcome::Fault : reports::Outcome::Stopped;
  if (!cycleReport.finish(millis(), outcome, holdInBandMs)) return;
  cycleReport.data.endUtc = utcNow();
  snprintf(cycleReport.data.reason, sizeof(cycleReport.data.reason), "%s", reason.c_str());
  lastReport = cycleReport.data; reportAvailable = true;
  // Best effort one write at the end; settings/safety reserve takes precedence.
  reportSaved = prefsReady && logs::makeRoom(logStore, persistedSeq, PERSIST_CAP,
                    logs::kReserveEntries + 2 + (sizeof(lastReport) + 31) / 32) &&
                prefs.putBytes("report", &lastReport, sizeof(lastReport)) == sizeof(lastReport);
  if (!reportSaved) logEvent("ATTENZIONE", "Report di fine ciclo disponibile solo in RAM: salvataggio NVS fallito", false);
}
void loadCycleReport() {
  reports::Snapshot candidate;
  if (!prefsReady || prefs.getBytesLength("report") != sizeof(candidate) ||
      prefs.getBytes("report", &candidate, sizeof(candidate)) != sizeof(candidate)) return;
  if ((candidate.version != 1 && candidate.version != reports::kVersion) || !candidate.stepCount || candidate.stepCount > reports::kMaxSteps ||
      candidate.completedSteps > candidate.stepCount || candidate.outcome == reports::Outcome::None ||
      static_cast<unsigned>(candidate.outcome) > static_cast<unsigned>(reports::Outcome::Fault)) return;
  candidate.recipeId[sizeof(candidate.recipeId) - 1] = 0;
  candidate.recipeName[sizeof(candidate.recipeName) - 1] = 0;
  candidate.reason[sizeof(candidate.reason) - 1] = 0;
  lastReport = candidate; reportAvailable = reportSaved = true;
}
void reportStatsJson(JsonObject item, const reports::Stats &stats) {
  item["samples"] = stats.samples; item["invalidPairs"] = stats.invalidPairs;
  auto sensors = item["sensors"].to<JsonArray>();
  for (size_t i = 0; i < 2; ++i) {
    auto sensor = sensors.add<JsonObject>(); sensor["validSamples"] = stats.valid[i];
    if (stats.valid[i]) {
      sensor["startC"] = stats.start[i]; sensor["endC"] = stats.end[i];
      sensor["minC"] = stats.low[i]; sensor["maxC"] = stats.high[i];
    } else { sensor["startC"] = nullptr; sensor["endC"] = nullptr; sensor["minC"] = nullptr; sensor["maxC"] = nullptr; }
  }
  if (std::isfinite(stats.maxDelta)) item["maxDeltaC"] = stats.maxDelta; else item["maxDeltaC"] = nullptr;
  item["trackingSamples"] = stats.trackingSamples;
  if (stats.trackingSamples) {
    item["meanAbsoluteErrorC"] = stats.absoluteErrorSum / stats.trackingSamples;
    item["maxOvershootC"] = stats.maxOvershoot; item["maxLagC"] = stats.maxLag;
    item["meanCommandPct"] = stats.dutySum / stats.trackingSamples;
    item["saturationPct"] = 100.0 * stats.saturatedSamples / stats.trackingSamples;
  } else {
    for (const char *key : {"meanAbsoluteErrorC", "maxOvershootC", "maxLagC", "meanCommandPct", "saturationPct"}) item[key] = nullptr;
  }
}
void reportApi() {
  if (!reportAvailable) { error(404, "Nessun report di fine ciclo disponibile"); return; }
  const auto &report = lastReport;
  JsonDocument doc;
  doc["version"] = report.version; doc["firmware"] = "full-1.3"; doc["saved"] = reportSaved;
  doc["holdBandC"] = report.version == 1 ? 1.0f : profile::kHoldBand;
  doc["bootCount"] = report.boot;
  doc["key"] = String(report.boot) + "-" + String(report.startMs) + "-" + String(report.endMs);
  doc["recipeId"] = report.recipeId; doc["recipeName"] = report.recipeName;
  doc["outcome"] = report.outcome == reports::Outcome::Completed ? "completed" : report.outcome == reports::Outcome::Fault ? "fault" : "stopped";
  doc["reason"] = report.reason;
  doc["startUptimeMs"] = report.startMs; doc["endUptimeMs"] = report.endMs;
  if (report.startUtc) doc["startUtc"] = report.startUtc; else doc["startUtc"] = nullptr;
  if (report.endUtc) doc["endUtc"] = report.endUtc; else doc["endUtc"] = nullptr;
  doc["durationMs"] = report.activeMs + report.pausedMs;
  doc["activeMs"] = report.activeMs; doc["pausedMs"] = report.pausedMs; doc["pauseCount"] = report.pauseCount;
  doc["completedSteps"] = report.completedSteps; doc["stepCount"] = report.stepCount;
  doc["curveAvailable"] = reportHistoryCount > 0;
  doc["curvePartial"] = reportHistoryPartial;
  doc["curveSamples"] = reportHistoryCount;
  doc["curveIntervalMs"] = HISTORY_MS;
  auto settings = doc["settings"].to<JsonObject>();
  settings["kp"] = report.kp; settings["ki"] = report.ki; settings["kd"] = report.kd;
  settings["actuator"] = report.ssr ? "ssr" : "relay";
  settings["windowSec"] = report.windowMs / 1000; settings["maxPower"] = report.maxPower;
  reportStatsJson(doc["stats"].to<JsonObject>(), report.stats);
  auto steps = doc["steps"].to<JsonArray>();
  for (size_t i = 0; i < report.stepCount; ++i) {
    const auto &result = report.steps[i]; auto item = steps.add<JsonObject>();
    item["index"] = i; item["type"] = typeName(result.requested.type); item["target"] = result.requested.target;
    item["rate"] = result.requested.rate; item["durationMin"] = result.requested.minutes;
    item["started"] = result.started; item["completed"] = result.completed;
    item["activeMs"] = result.activeMs; item["pausedMs"] = result.pausedMs;
    item["holdInBandMs"] = result.holdInBandMs;
    reportStatsJson(item["stats"].to<JsonObject>(), result.stats);
  }
  sendJson(200, doc);
}
void reportHistoryApi() {
  if (!reportAvailable) { error(404, "Nessun report disponibile"); return; }
  const String key = String(lastReport.boot) + "-" + String(lastReport.startMs) + "-" + String(lastReport.endMs);
  if (!server.hasArg("key") || server.arg("key") != key) {
    error(409, "Il report è cambiato: aggiorna prima di esportare"); return;
  }
  JsonDocument doc;
  doc["key"] = key; doc["available"] = reportHistoryCount > 0;
  doc["partial"] = reportHistoryPartial; doc["intervalMs"] = HISTORY_MS;
  doc["bootCount"] = lastReport.boot;
  uint32_t after = server.hasArg("after") ? strtoul(server.arg("after").c_str(), nullptr, 10) : 0;
  size_t first = 0;
  while (first < reportHistoryCount && reportHistory[first].seq <= after) ++first;
  const size_t end = first + HISTORY_PAGE < reportHistoryCount ? first + HISTORY_PAGE : reportHistoryCount;
  JsonArray list = doc["samples"].to<JsonArray>();
  for (size_t i = first; i < end; ++i) {
    const auto &s = reportHistory[i]; JsonArray row = list.add<JsonArray>();
    row.add(s.seq); row.add(s.ms);
    if (s.flags & 1) row.add(s.t1); else row.add(nullptr);
    if (s.flags & 2) row.add(s.t2); else row.add(nullptr);
    if (s.flags & 8) row.add(s.setpoint); else row.add(nullptr);
    row.add(s.duty); row.add(s.flags); row.add(s.step);
  }
  doc["more"] = end < reportHistoryCount;
  sendJson(200, doc);
}
void recipesApi() {
  JsonDocument doc; JsonArray list = doc["recipes"].to<JsonArray>();
  for (size_t i = 0; i < recipeCount; ++i) recipeToJson(list.add<JsonObject>(), recipes[i]);
  sendJson(200, doc);
}
bool parseRecipes(JsonVariantConst list, Recipe *out, size_t &count, String &reason) {
  if (!list.is<JsonArrayConst>()) { reason = "Elenco programmi non valido"; return false; }
  JsonArrayConst arr = list.as<JsonArrayConst>();
  if (arr.size() == 0 || arr.size() > MAX_RECIPES) { reason = "Servono 1-8 programmi"; return false; }
  count = 0;
  for (JsonVariantConst value : arr) {
    if (!value.is<JsonObjectConst>()) { reason = "Programma non valido"; return false; }
    JsonObjectConst item = value.as<JsonObjectConst>();
    const char *id = item["id"] | "", *name = item["name"] | "";
    size_t idLen = strlen(id), nameLen = strlen(name);
    if (!idLen || idLen > 32 || !nameLen || nameLen > 48) { reason = "ID o nome programma non valido"; return false; }
    for (size_t j = 0; j < idLen; ++j)
      if (!isalnum(static_cast<unsigned char>(id[j])) && id[j] != '-' && id[j] != '_') {
        reason = "ID programma: usa lettere, numeri, - o _"; return false;
      }
    for (size_t j = 0; j < count; ++j) if (out[j].id == id) { reason = "ID programma duplicato"; return false; }
    JsonArrayConst steps = item["steps"].as<JsonArrayConst>();
    if (steps.isNull() || steps.size() == 0 || steps.size() > MAX_STEPS) { reason = "Servono 1-12 step per programma"; return false; }
    Recipe &recipe = out[count]; recipe.id = id; recipe.name = name; recipe.count = 0;
    float previousTarget = NAN;
    for (JsonVariantConst entry : steps) {
      if (!entry.is<JsonObjectConst>()) { reason = "Step non valido"; return false; }
      JsonObjectConst data = entry.as<JsonObjectConst>();
      const char *kind = data["type"] | "";
      profile::Type type;
      if (!strcmp(kind, "ramp")) type = profile::Type::Ramp;
      else if (!strcmp(kind, "hold")) type = profile::Type::Hold;
      else if (!strcmp(kind, "cooldown")) type = profile::Type::Cooldown;
      else { reason = "Tipo step sconosciuto"; return false; }
      if (!data["target"].is<float>() && !data["target"].is<int>()) { reason = "Target mancante"; return false; }
      float target = data["target"].as<float>();
      if (!thermal::validTarget(target)) {
        reason = "Target consentito: 0-" + String(thermal::kMaxTarget, 0) + " °C"; return false;
      }
      profile::Step step{type, target, 0, 0};
      if (type == profile::Type::Hold) {
        if (!data["duration"].is<int>()) { reason = "Durata hold mancante"; return false; }
        int minutes = data["duration"].as<int>();
        if (minutes < 1 || minutes > 360) { reason = "Hold consentito: 1-360 min"; return false; }
        step.minutes = minutes;
      } else {
        if (!data["rate"].is<float>() && !data["rate"].is<int>()) { reason = "Rampa °C/min mancante"; return false; }
        step.rate = data["rate"].as<float>();
        if (!std::isfinite(step.rate) || step.rate < 0.1f) {
          reason = "Rate minimo: 0,1 °C/min"; return false;
        }
      }
      if (std::isfinite(previousTarget)) {
        if (type == profile::Type::Ramp && target <= previousTarget) { reason = "Ramp deve salire rispetto allo step precedente"; return false; }
        if (type == profile::Type::Cooldown && target >= previousTarget) { reason = "Cooldown deve scendere rispetto allo step precedente"; return false; }
        if (type == profile::Type::Hold && fabsf(target - previousTarget) > 0.1f) {
          reason = "Hold deve usare il target dello step precedente"; return false;
        }
      }
      previousTarget = target;
      recipe.steps[recipe.count++] = step;
    }
    ++count;
  }
  return true;
}
void saveRecipesApi() {
  if (phase == Phase::Running || phase == Phase::Paused) { error(409, "Ferma il ciclo prima di cambiare i programmi"); return; }
  if (server.arg("plain").length() > 16000) { error(413, "Programmi troppo grandi"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) { error(400, "JSON non valido"); return; }
  static Recipe proposed[MAX_RECIPES]; size_t count = 0; String reason;
  if (!parseRecipes(doc["recipes"], proposed, count, reason)) { error(400, reason); return; }
  JsonDocument stored; JsonArray list = stored.to<JsonArray>();
  for (size_t i = 0; i < count; ++i) recipeToJson(list.add<JsonObject>(), proposed[i]);
  String encoded; serializeJson(stored, encoded);
  if (encoded.length() > 12000 || prefs.putString("recipes", encoded) != encoded.length()) {
    error(507, "Salvataggio programmi fallito"); return;
  }
  recipeCount = count;
  for (size_t i = 0; i < count; ++i) recipes[i] = proposed[i];
  logEvent("PROGRAMMI", "Salvati " + String(count) + " programmi");
  recipesApi();
}
void statusApi(uint32_t testToken = 0) {
  JsonDocument doc;
  const auto output = outputSnapshot();
  JsonObject test = doc["test"].to<JsonObject>();
  test["enabled"] = output.diagnostic; test["running"] = output.test.mode != TestMode::Idle;
  test["name"] = testModeName(output.test.mode); test["remainingMs"] = output.test.remaining(millis());
  test["leaseMs"] = OutputTest::LEASE_MS; test["interrupted"] = testInterrupted;
  test["historyLatestSeq"] = testHistorySeq; test["historyCapacity"] = testHistoryCapacity;
  test["runSeq"] = testRunSeq; test["startedAtMs"] = testStartedAtMs;
  if (testToken) test["token"] = testToken;
  doc["red"] = output.red; doc["green"] = output.green;
  doc["otaEnabled"] = otaEnabled; doc["otaUpdating"] = output.updating;
  doc["otaHostname"] = MDNS_NAME; doc["otaPort"] = OTA_PORT;
  bool stationReady = WiFi.status() == WL_CONNECTED;
  doc["mode"] = "FULL"; doc["firmware"] = "full-1.3"; doc["phase"] = phaseName(); doc["relay"] = heaterOn();
  doc["emergency"] = output.emergency;
  doc["ready"] = !output.diagnostic && !output.updating && (apReady || stationReady) && sensorsHealthy() && phase != Phase::Fault &&
                 !output.emergency && heaterFault() == heater::Fault::None && uint32_t(millis() - sampleAtMs) < heater::Guard::kStaleMs;
  doc["actuator"] = controlSettings.ssr ? "ssr" : "relay";
  doc["windowSec"] = controlSettings.windowMs / 1000; doc["maxPower"] = controlSettings.maxPower;
  doc["cycleElapsedSec"] = cycleActive() ? uint32_t(millis() - cycleStarted) / 1000 :
    activeRecipe.count && reportAvailable && lastReport.startMs == cycleStarted && lastReport.boot == bootCount ?
      uint32_t(lastReport.endMs - lastReport.startMs) / 1000 : 0;
  doc["stepRemainingSec"] = nullptr;
  doc["stepRemainingEstimated"] = false;
  if (activeRecipe.count) doc["cycleStartedAtMs"] = cycleStarted; else doc["cycleStartedAtMs"] = nullptr;
  doc["stepElapsedSec"] = phase == Phase::Running ? uint32_t(millis() - stepStarted) / 1000 :
                           phase == Phase::Paused ? uint32_t(pausedAt - stepStarted) / 1000 : 0;
  doc["freeHeap"] = ESP.getFreeHeap(); doc["minFreeHeap"] = ESP.getMinFreeHeap();
  doc["rssi"] = stationReady ? WiFi.RSSI() : 0;
  doc["sampleAgeMs"] = uint32_t(millis() - sampleAtMs);
  doc["duty"] = duty;
  JsonObject pid = doc["pid"].to<JsonObject>();
  pid["kp"] = kp; pid["ki"] = ki; pid["kd"] = kd;
  if (std::isfinite(setpointC)) doc["target"] = setpointC; else doc["target"] = nullptr;
  doc["stepIndex"] = stepIndex;
  doc["stepCount"] = activeRecipe.count;
  doc["recipeName"] = activeRecipe.name;
  if (activeRecipe.count && stepIndex < activeRecipe.count) {
    const auto &step = activeRecipe.steps[stepIndex];
    doc["stepType"] = typeName(step.type); doc["stepFinalTarget"] = step.target;
    doc["stepRate"] = step.type == profile::Type::Hold ? 0 : step.rate;
    doc["holdInBandSec"] = holdInBandMs / 1000;
    doc["holdRequiredSec"] = uint32_t(step.minutes) * 60;
    if (cycleActive()) {
      const uint32_t elapsed = uint32_t((phase == Phase::Paused ? pausedAt : millis()) - stepStarted);
      const float remaining = profile::remainingSec(step, stepStartC, elapsed, holdInBandMs,
        sensors[0].valid && sensors[1].valid ? fminf(sensors[0].c, sensors[1].c) : NAN,
        sensors[0].valid && sensors[1].valid ? hottestC() : NAN);
      if (std::isfinite(remaining)) doc["stepRemainingSec"] = remaining;
      doc["stepRemainingEstimated"] = step.type != profile::Type::Hold;
    }
  }
  doc["fault"] = faultText; doc["bootCount"] = bootCount;
  doc["lastCycleError"] = lastCycleError;
  doc["resetReason"] = resetReason;
  doc["interrupted"] = hadInterrupted;
  doc["outageUpperBoundSec"] = outageUpperBoundSec;
  JsonObject interruption = doc["lastInterruption"].to<JsonObject>();
  interruption["recorded"] = interruptionRecorded;
  interruption["resetReason"] = lastInterruption.resetReason;
  interruption["upperBoundSec"] = lastInterruption.checkpointUtc && lastInterruption.rebootUtc >= lastInterruption.checkpointUtc ?
    lastInterruption.rebootUtc - lastInterruption.checkpointUtc : 0;
  doc["ntpSynced"] = ntpSynced;
  doc["utcSec"] = utcNow();
  doc["workshopConfigured"] = WORKSHOP_WIFI_SSID[0] != '\0';
  doc["workshopConnected"] = stationReady;
  doc["stationIp"] = stationReady ? WiFi.localIP().toString() : "";
  doc["apIp"] = apReady ? WiFi.softAPIP().toString() : "";
  doc["localName"] = mdnsReady ? String(MDNS_NAME) + ".local" : "";
  doc["uptimeMs"] = millis(); doc["sampleAtMs"] = sampleAtMs;
  if (std::isfinite(chipTemperatureC)) doc["chipTemperatureC"] = chipTemperatureC;
  else doc["chipTemperatureC"] = nullptr;
  doc["reportAvailable"] = reportAvailable;
  doc["reportKey"] = reportAvailable ? String(lastReport.boot) + "-" + String(lastReport.startMs) + "-" + String(lastReport.endMs) : "";
  doc["historyLatestSeq"] = historySeq;
  JsonArray list = doc["sensors"].to<JsonArray>();
  for (const auto &sensor : sensors) {
    JsonObject item = list.add<JsonObject>();
    if (sensor.valid) item["c"] = sensor.c; else item["c"] = nullptr;
    item["valid"] = sensor.valid; item["faultCode"] = sensor.fault;
    item["raw"] = sensor.raw; item["ohms"] = sensor.ohms; item["message"] = sensorMessage(sensor);
    item["rref"] = &sensor == &sensors[0] ? RREF_1 : RREF_2;
  }
  sendJson(200, doc);
}
void pidApi() {
  if (phase == Phase::Running || phase == Phase::Paused) {
    error(409, "Ferma il ciclo prima di modificare il PID"); return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) { error(400, "JSON non valido"); return; }
  const char *keys[] = {"kp", "ki", "kd"};
  for (const char *key : keys) {
    if (!doc[key].is<float>() && !doc[key].is<int>()) { error(400, "Inserisci Kp, Ki e Kd numerici"); return; }
  }
  float p = doc["kp"].as<float>(), i = doc["ki"].as<float>(), d = doc["kd"].as<float>();
  if (!std::isfinite(p) || !std::isfinite(i) || !std::isfinite(d) ||
      p < 0 || p > 50 || i < 0 || i > 0.5f || d < 0 || d > 200) {
    error(400, "Limiti: Kp 0-50, Ki 0-0,5, Kd 0-200"); return;
  }
  PidGains gains{p, i, d};
  if (prefs.putBytes("pid", &gains, sizeof(gains)) != sizeof(gains)) {
    error(507, "Salvataggio PID fallito"); return;
  }
  kp = p; ki = i; kd = d; resetPid();
  logEvent("PID", "Coefficienti salvati: Kp=" + String(kp, 3) +
           " Ki=" + String(ki, 4) + " Kd=" + String(kd, 3));
  statusApi();
}
void controlSettingsApi() {
  if (phase == Phase::Running || phase == Phase::Paused) { error(409, "Ferma il ciclo prima di modificare il comando"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) { error(400, "JSON non valido"); return; }
  String actuator = doc["actuator"] | "";
  if ((actuator != "ssr" && actuator != "relay") || !doc["windowSec"].is<uint32_t>() ||
      (!doc["maxPower"].is<float>() && !doc["maxPower"].is<int>())) {
    error(400, "Inserisci attuatore, finestra intera in secondi e limite potenza"); return;
  }
  uint32_t seconds = doc["windowSec"].as<uint32_t>();
  float maxPower = doc["maxPower"].as<float>();
  if (seconds > 300 || !heater::validSettings(actuator == "ssr", maxPower, seconds * 1000)) {
    error(400, "Potenza 1-100%; finestra SSR 1-300 s, rele meccanico 60-300 s"); return;
  }
  ControlSettings proposed{}; proposed.ssr = actuator == "ssr";
  proposed.windowMs = seconds * 1000; proposed.maxPower = maxPower;
  if (prefs.putBytes("control", &proposed, sizeof(proposed)) != sizeof(proposed)) { error(507, "Salvataggio comando fallito"); return; }
  controlSettings = proposed; resetPid();
  logEvent("COMANDO", String("Attuatore ") + actuator + "; finestra " + String(seconds) + " s; potenza max " + String(maxPower, 1) + "%");
  statusApi();
}
void commandApi() {
  drainHeaterNotices();
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) { error(400, "JSON non valido"); return; }
  String action = doc["action"] | "";
  if (action == "emergency") {
    // Stop physical commands before NVS writes, report generation or logging.
    portENTER_CRITICAL(&heaterMux); outputs.emergencyStop(); ownerToken = 0; writeOutputs(); portEXIT_CRITICAL(&heaterMux);
    duty = 0;
    if (testActiveSaved) { prefs.putBool("testActive", false); testActiveSaved = false; }
    const bool saved = prefs.putBool("emergency", true) == sizeof(bool);
    trip("EMERGENZA: tutte le uscite spente; riconoscimento richiesto");
    logEvent("EMERGENZA", saved ? "Blocco uscite registrato; riconoscimento manuale richiesto" :
      "Blocco uscite attivo in RAM; salvataggio NVS fallito");
    statusApi(); return;
  }
  if (action == "stop") {
    portENTER_CRITICAL(&heaterMux); outputs.stopAll(); ownerToken = 0; writeOutputs(); portEXIT_CRITICAL(&heaterMux);
    stopTests("STOP manuale: test fermato"); inhibitHeater();
    logEvent("STOP", "STOP manuale: tutte le uscite spente");
    if (phase == Phase::Running || phase == Phase::Paused) stopCycle(Phase::Idle, "Ciclo fermato manualmente");
    statusApi(); return;
  }
  if (outputSnapshot().updating) { error(503, "Aggiornamento OTA in corso"); return; }
  if (action != "stop" && diagnosticMode()) { error(409, "Esci dalla modalita TEST prima di comandare il ciclo"); return; }
  if (action == "reset") {
    if (phase == Phase::Running || phase == Phase::Paused) { error(409, "Ferma prima il ciclo"); return; }
    readSensor(rtd1, sensors[0]); readSensor(rtd2, sensors[1]); sampleAtMs = millis();
    if (!sensorsHealthy()) { error(409, "Sonde o temperature fuori limite"); return; }
    if (outputSnapshot().emergency && prefs.putBool("emergency", false) != sizeof(bool)) {
      error(507, "Riconoscimento emergenza non salvato: uscite ancora bloccate"); return;
    }
    portENTER_CRITICAL(&heaterMux); outputs.acknowledgeEmergency(); outputs.guard.clear(); outputs.stopAll(); writeOutputs(); portEXIT_CRITICAL(&heaterMux);
    phase = Phase::Idle; faultText = "";
    logEvent("INFO", "Allarme/interruzione riconosciuti dall'operatore"); statusApi(); return;
  }
  if (action == "pause") {
    if (phase != Phase::Running) { error(409, "Nessun ciclo da mettere in pausa"); return; }
    phase = Phase::Paused; pausedAt = millis(); cycleReport.setPaused(pausedAt, true); inhibitHeater(); duty = 0;
    holdTimer.update(pausedAt, false, false);
    applyWindow();
    logEvent("CICLO", "Ciclo in pausa: uscita spenta"); statusApi(); return;
  }
  if (action == "resume") {
    if (phase != Phase::Paused) { error(409, "Nessun ciclo in pausa"); return; }
    readSensor(rtd1, sensors[0]); readSensor(rtd2, sensors[1]); sampleAtMs = millis();
    if (!sensorsHealthy() || heaterFault() != heater::Fault::None) { error(409, "Sonde/controllo fuori limite: riconosci l'allarme"); return; }
    if (uint32_t(millis() - cycleStarted) >= MAX_CYCLE_MS) { trip("Tempo massimo ciclo 12 ore superato"); statusApi(); return; }
    uint32_t pausedMs = uint32_t(millis() - pausedAt);
    stepStarted += pausedMs;
    cycleReport.setPaused(millis(), false);
    phase = Phase::Running; resetPid();
    logEvent("CICLO", "Ciclo ripreso dalla pausa; stesso step"); sampleAndControl(); statusApi(); return;
  }
  if (action == "start") {
    if (phase == Phase::Running || phase == Phase::Paused) { error(409, "Ciclo gia in corso"); return; }
    if (phase == Phase::Fault || phase == Phase::Interrupted) { error(409, "Riconosci prima l'allarme o l'interruzione"); return; }
    if (heaterFault() != heater::Fault::None) { error(409, "Supervisore in blocco: riconosci l'allarme"); return; }
    if (!doc["hardwareReady"].is<bool>() || !doc["hardwareReady"].as<bool>()) {
      error(400, "Conferma collaudo hardware e protezioni indipendenti prima di avviare"); return;
    }
    // Read again here: a stale status must never authorize a heat command.
    readSensor(rtd1, sensors[0]); readSensor(rtd2, sensors[1]);
    sampleAtMs = millis();
    if (!sensorsHealthy()) { error(409, "Entrambe le PT100 reali devono essere valide e concordi"); return; }
    const char *id = doc["recipeId"] | "";
    int selected = -1;
    for (size_t i = 0; i < recipeCount; ++i) if (recipes[i].id == id) { selected = int(i); break; }
    if (selected < 0) { error(404, "Programma non trovato"); return; }
    activeRecipe = recipes[selected];
    if (activeRecipe.steps[0].type == profile::Type::Ramp &&
        meanC() >= activeRecipe.steps[0].target - profile::kBand) {
      error(409, "Il forno e gia vicino/sopra il primo target: scegli un programma adatto"); return;
    }
    if (activeRecipe.steps[0].type == profile::Type::Cooldown &&
        meanC() <= activeRecipe.steps[0].target + profile::kBand) {
      error(409, "Il forno e gia sotto il target di cooldown"); return;
    }
    if (prefs.putULong64("checkpoint", 0) != sizeof(uint64_t) ||
        prefs.putBool("active", true) != sizeof(bool)) {
      error(507, "Memoria di sicurezza non disponibile: ciclo non avviato"); return;
    }
    phase = Phase::Running; stepIndex = 0; cycleStarted = millis();
    cycleReport.begin(cycleStarted, activeRecipe.steps, activeRecipe.count);
    auto &report = cycleReport.data;
    report.boot = bootCount; report.startUtc = utcNow();
    snprintf(report.recipeId, sizeof(report.recipeId), "%s", activeRecipe.id.c_str());
    snprintf(report.recipeName, sizeof(report.recipeName), "%s", activeRecipe.name.c_str());
    report.kp = kp; report.ki = ki; report.kd = kd; report.ssr = controlSettings.ssr;
    report.windowMs = controlSettings.windowMs; report.maxPower = controlSettings.maxPower;
    checkpointAt = millis() - CHECKPOINT_MS;
    logEvent("CICLO", "Ciclo partito: " + activeRecipe.name + "; PT100 reali");
    beginStep(); recordHistory(true, HISTORY_START); sampleAndControl(); statusApi(); return;
  }
  error(400, "Azione sconosciuta");
}
void logsApi() {
  JsonDocument doc; doc["bootCount"] = bootCount; JsonArray list = doc["events"].to<JsonArray>();
  for (size_t i = 0; i < eventCount; ++i) {
    const Event &event = events[(eventHead + EVENT_CAP - eventCount + i) % EVENT_CAP];
    JsonObject item = list.add<JsonObject>();
    item["seq"] = event.seq; item["atMs"] = event.at;
    if (event.utc) item["utc"] = event.utc; else item["utc"] = nullptr;
    item["level"] = event.level; item["message"] = event.message; item["saved"] = event.saved;
  }
  doc["capacity"] = EVENT_CAP; doc["savedCapacity"] = PERSIST_CAP; sendJson(200, doc);
}
void historyApi() {
  JsonDocument doc; doc["bootCount"] = bootCount; doc["capacity"] = HISTORY_CAP;
  doc["intervalMs"] = HISTORY_MS;
  doc["oldestSeq"] = historyCount ? history[(historyHead + HISTORY_CAP - historyCount) % HISTORY_CAP].seq : 0;
  doc["latestSeq"] = historySeq;
  JsonArray list = doc["samples"].to<JsonArray>();
  size_t first = 0, end = historyCount;
  if (server.hasArg("before")) {
    uint32_t before = strtoul(server.arg("before").c_str(), nullptr, 10);
    while (end && history[(historyHead + HISTORY_CAP - historyCount + end - 1) % HISTORY_CAP].seq >= before) --end;
    first = end > HISTORY_PAGE ? end - HISTORY_PAGE : 0;
  } else if (server.hasArg("tail")) first = end > HISTORY_PAGE ? end - HISTORY_PAGE : 0;
  else {
    uint32_t after = server.hasArg("after") ? strtoul(server.arg("after").c_str(), nullptr, 10) : 0;
    while (first < end && history[(historyHead + HISTORY_CAP - historyCount + first) % HISTORY_CAP].seq <= after) ++first;
    if (end - first > HISTORY_PAGE) end = first + HISTORY_PAGE;
  }
  for (size_t i = first; i < end; ++i) {
    const Sample &s = history[(historyHead + HISTORY_CAP - historyCount + i) % HISTORY_CAP];
    JsonArray row = list.add<JsonArray>();
    row.add(s.seq); row.add(s.ms);
    if (s.flags & 1) row.add(s.t1); else row.add(nullptr);
    if (s.flags & 2) row.add(s.t2); else row.add(nullptr);
    if (s.flags & 8) row.add(s.setpoint); else row.add(nullptr);
    row.add(s.duty); row.add(s.flags); row.add(s.step);
  }
  doc["moreBefore"] = first > 0; doc["more"] = end < historyCount; sendJson(200, doc);
}
void recordTestHistory(bool force = false) {
  uint32_t now = millis();
  if (!testHistoryCapacity) return;
  if (!force && testHistoryCount && uint32_t(now - lastTestHistoryAt) < 1000) return;
  lastTestHistoryAt = now;
  const auto state = outputSnapshot();
  const uint8_t flags = (sensors[0].valid ? 1 : 0) | (sensors[1].valid ? 2 : 0) |
                        (state.relay ? 4 : 0) | (state.red ? 8 : 0) | (state.green ? 16 : 0);
  testHistory[testHistoryHead] = {++testHistorySeq, force ? testStartedAtMs : sampleAtMs, sensors[0].c, sensors[1].c,
    sensors[0].raw, sensors[1].raw, sensors[0].fault, sensors[1].fault, flags};
  testHistoryHead = (testHistoryHead + 1) % testHistoryCapacity;
  if (testHistoryCount < testHistoryCapacity) ++testHistoryCount;
}
void testHistoryApi() {
  JsonDocument doc; doc["bootCount"] = bootCount; doc["capacity"] = testHistoryCapacity;
  doc["intervalMs"] = 1000; doc["latestSeq"] = testHistorySeq;
  doc["oldestSeq"] = testHistoryCount ? testHistory[(testHistoryHead + testHistoryCapacity - testHistoryCount) % testHistoryCapacity].seq : 0;
  auto arr = doc["samples"].to<JsonArray>();
  size_t first = 0, end = testHistoryCount;
  if (server.hasArg("before")) {
    uint32_t before = strtoul(server.arg("before").c_str(), nullptr, 10);
    while (end && testHistory[(testHistoryHead + testHistoryCapacity - testHistoryCount + end - 1) % testHistoryCapacity].seq >= before) --end;
    first = end > TEST_HISTORY_PAGE ? end - TEST_HISTORY_PAGE : 0;
  } else if (server.hasArg("tail")) first = end > TEST_HISTORY_PAGE ? end - TEST_HISTORY_PAGE : 0;
  else {
    uint32_t after = server.hasArg("after") ? strtoul(server.arg("after").c_str(), nullptr, 10) : 0;
    while (first < end && testHistory[(testHistoryHead + testHistoryCapacity - testHistoryCount + first) % testHistoryCapacity].seq <= after) ++first;
    if (end - first > TEST_HISTORY_PAGE) end = first + TEST_HISTORY_PAGE;
  }
  for (size_t i = first; i < end; ++i) {
    const auto &s = testHistory[(testHistoryHead + testHistoryCapacity - testHistoryCount + i) % testHistoryCapacity];
    auto row = arr.add<JsonArray>(); row.add(s.seq); row.add(s.ms);
    if (s.flags & 1) row.add(s.t1); else row.add(nullptr);
    if (s.flags & 2) row.add(s.t2); else row.add(nullptr);
    row.add(s.raw1); row.add(s.raw2); row.add(s.fault1); row.add(s.fault2); row.add(s.flags);
  }
  doc["moreBefore"] = first > 0; doc["more"] = end < testHistoryCount; sendJson(200, doc);
}
void testCommandApi() {
  JsonDocument input;
  if (deserializeJson(input, server.arg("plain")) || !input["action"].is<const char *>()) {
    error(400, "Richiesta JSON non valida"); return;
  }
  const String action = input["action"].as<String>();
  // Test-only STOP cannot affect an autonomous recipe started by another page.
  if (action == "stop" || action == "exit") {
    if (!diagnosticMode()) { statusApi(); return; }
    stopTests(action == "exit" ? "Modalita TEST terminata: uscita spenta" : "STOP test: uscita spenta", action == "exit");
    statusApi(); return;
  }
  if (cycleActive()) { error(409, "Ferma il ciclo prima di usare TEST (anche in pausa)"); return; }
  if (outputSnapshot().emergency) { error(409, "Emergenza attiva: riconosci il blocco prima di usare TEST"); return; }
  if (outputSnapshot().updating) { error(503, "Aggiornamento OTA in corso: test disabilitati"); return; }
  if (action == "enter") {
    portENTER_CRITICAL(&heaterMux); outputs.enterTest(false); portEXIT_CRITICAL(&heaterMux);
    statusApi(); return;
  }
  TestMode mode = TestMode::Idle;
  if (action == "relayPulse") mode = TestMode::RelayPulse;
  else if (action == "relaySequence") mode = TestMode::RelaySequence;
  else if (action == "ledGreen") mode = TestMode::LedGreen;
  else if (action == "ledRed") mode = TestMode::LedRed;
  else if (action == "ledBoth") mode = TestMode::LedBoth;
  else if (action == "ledOff") mode = TestMode::LedOff;
  else if (action == "ledSequence") mode = TestMode::LedSequence;
  else { error(400, "Azione test sconosciuta"); return; }
  uint32_t duration = 1000;
  if (mode == TestMode::RelayPulse) {
    if (!input["durationMs"].is<uint32_t>()) { error(400, "Durata intera richiesta in ms"); return; }
    duration = input["durationMs"].as<uint32_t>();
    if (!OutputTest::validRelayDuration(duration)) { error(400, "Durata ammessa: 100..5000 ms, 30 s, 1 min, 2 min o 10 min"); return; }
  }
  if (outputSnapshot().test.mode != TestMode::Idle) { error(409, "Test in corso: attendi o premi STOP"); return; }
  if (prefs.putBool("testActive", true) != sizeof(bool)) { error(507, "Memoria di sicurezza non disponibile: test non avviato"); return; }
  testActiveSaved = true;
  const uint32_t token = esp_random() | 1U;
  const uint32_t startedAt = millis();
  portENTER_CRITICAL(&heaterMux);
  const bool started = outputs.startTest(mode, startedAt, duration, false);
  if (started) { ownerToken = token; outputs.tick(millis(), outputServerReady, outputAlarm); writeOutputs(); }
  portEXIT_CRITICAL(&heaterMux);
  if (!started) { prefs.putBool("testActive", false); testActiveSaved = false; error(409, "Test non disponibile"); return; }
  ++testRunSeq; testStartedAtMs = startedAt;
  recordTestHistory(true);
  logEvent("TEST", String(testModeName(mode)) + (mode == TestMode::RelayPulse ? " " + String(duration) + " ms" : "") + "; nessun feedback fisico dell'uscita");
  statusApi(token);
}
void testHeartbeatApi() {
  JsonDocument input;
  if (deserializeJson(input, server.arg("plain")) || !input["token"].is<uint32_t>()) { error(400, "Token richiesto"); return; }
  const uint32_t token = input["token"].as<uint32_t>();
  portENTER_CRITICAL(&heaterMux);
  bool ok = token != 0 && token == ownerToken && !outputs.updating && outputs.test.heartbeat(millis());
  portEXIT_CRITICAL(&heaterMux);
  if (!ok) { error(403, "Test terminato o appartenente a un'altra dashboard"); return; }
  server.sendHeader("Cache-Control", "no-store"); server.send(204);
}
void setupOta() {
  if (!OVEN_OTA_PASSWORD[0]) { logEvent("OTA", "OTA disabilitato: configura include/ota_config.h"); return; }
  ArduinoOTA.setHostname(MDNS_NAME); ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setPassword(OVEN_OTA_PASSWORD); ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.onStart([] {
    portENTER_CRITICAL(&heaterMux); outputs.beginUpdate(); ownerToken = 0; writeOutputs(); portEXIT_CRITICAL(&heaterMux);
    stopTests("Aggiornamento OTA: test fermato");
    stopCycle(Phase::Idle, "Aggiornamento OTA: ciclo fermato e uscita spenta");
    logEvent("OTA", "Aggiornamento avviato: tutte le uscite bloccate");
  });
  ArduinoOTA.onEnd([] { logEvent("OTA", "Aggiornamento completato: riavvio con uscite spente"); });
  ArduinoOTA.onError([](ota_error_t code) {
    portENTER_CRITICAL(&heaterMux); outputs.stopAll(); ownerToken = 0; writeOutputs(); portEXIT_CRITICAL(&heaterMux);
    stopTests("Errore OTA: test fermato", true);
    stopCycle(Phase::Idle, "Errore OTA: ciclo fermato e uscita spenta");
    portENTER_CRITICAL(&heaterMux); outputs.updating = false; portEXIT_CRITICAL(&heaterMux);
    logEvent("OTA", "Errore aggiornamento, codice " + String(static_cast<unsigned>(code)) + "; ripartenza solo manuale");
  });
  ArduinoOTA.begin(); otaEnabled = true;
  logEvent("OTA", "Upload Wi-Fi con password attivo, porta " + String(OTA_PORT));
}
void updateNetwork() {
  if (uint32_t(millis() - lastNetworkCheck) < 1000) return;
  lastNetworkCheck = millis();
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    String ip = WiFi.localIP().toString();
    if (!stationWasConnected || stationIp != ip) {
      stationWasConnected = true; stationIp = ip;
      logEvent("RETE", "Dashboard Wi-Fi capannone: http://" + ip + "/");
    }
  } else if (stationWasConnected) {
    stationWasConnected = false; stationIp = "";
    logEvent("ATTENZIONE", "Wi-Fi capannone disconnesso; rete diretta ancora attiva");
  }
  if (!mdnsReady && (apReady || connected)) {
    mdnsReady = MDNS.begin(MDNS_NAME);
    if (mdnsReady) { MDNS.addService("http", "tcp", 80); if (otaEnabled) MDNS.enableArduino(OTA_PORT, true); }
  }
  portENTER_CRITICAL(&heaterMux); outputServerReady = apReady || connected; portEXIT_CRITICAL(&heaterMux);
  if (connected && !ntpStarted) {
    configTime(0, 0, WORKSHOP_NTP_SERVER); ntpStarted = true;
    logEvent("RETE", "Sincronizzazione ora NTP in corso");
  }
  if (ntpStarted && !ntpSynced && time(nullptr) >= 1700000000 &&
      sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
    ntpSynced = true; logEvent("RETE", "Ora NTP sincronizzata");
    if (hadInterrupted && previousCheckpoint) {
      uint64_t bootEpoch = uint64_t(time(nullptr)) - millis() / 1000;
      if (bootEpoch >= previousCheckpoint) {
        outageUpperBoundSec = bootEpoch - previousCheckpoint;
        lastInterruption.rebootUtc = bootEpoch;
        prefs.putBytes("interruption", &lastInterruption, sizeof(lastInterruption));
        logEvent("ATTENZIONE", "Ciclo interrotto; tempo massimo senza controllo " +
                 String((unsigned long)outageUpperBoundSec) + " s (non durata esatta del blackout)");
      }
    }
  }
}
void loadRecipes() {
  static Recipe loaded[MAX_RECIPES]; size_t count = 0; String reason;
  String saved = prefs.isKey("recipes") ? prefs.getString("recipes", "") : ""; JsonDocument doc;
  if (saved.length() && !deserializeJson(doc, saved) && parseRecipes(doc.as<JsonVariantConst>(), loaded, count, reason)) {
    recipeCount = count; for (size_t i = 0; i < count; ++i) recipes[i] = loaded[i]; return;
  }
  recipes[0].id = "prova-50"; recipes[0].name = "Prova a 50 C"; recipes[0].count = 3;
  recipes[0].steps[0] = {profile::Type::Ramp, 50, 1.0f, 0};
  recipes[0].steps[1] = {profile::Type::Hold, 50, 0, 10};
  recipes[0].steps[2] = {profile::Type::Cooldown, 35, 1.0f, 0};
  recipeCount = 1;
  if (saved.length()) logEvent("ALLARME", "Programmi salvati non validi: caricato il programma di prova");
}
void setup() {
  digitalWrite(HEAT, RELAY_ACTIVE_HIGH ? LOW : HIGH); pinMode(HEAT, OUTPUT);
  digitalWrite(RED, LED_ACTIVE_HIGH ? LOW : HIGH); pinMode(RED, OUTPUT);
  digitalWrite(GREEN, LED_ACTIVE_HIGH ? LOW : HIGH); pinMode(GREEN, OUTPUT);
  Serial.begin(115200);
  heaterNotices = xQueueCreate(16, sizeof(HeaterNotice));
  if (!heaterNotices || xTaskCreate(heaterTask, "heater-guard", 3072, nullptr, 2, nullptr) != pdPASS) {
    Serial.println("Supervisore non avviato: firmware fermo, uscita spenta");
    for (;;) delay(1000);
  }
  prefsReady = prefs.begin("oven-full", false);
  if (!prefsReady) Serial.println("[ALLARME] Memoria NVS non disponibile; salvataggi e avvio ciclo bloccati");
  loadEvents(); loadCycleReport();
  lastCycleError = prefs.getString("cycleError", "");
  if (lastCycleError.isEmpty() && reportAvailable && lastReport.outcome == reports::Outcome::Fault)
    lastCycleError = lastReport.reason;
  reportHistory = static_cast<Sample *>(ps_malloc(sizeof(Sample) * (HISTORY_CAP + 1)));
  if (!reportHistory) reportHistory = static_cast<Sample *>(calloc(HISTORY_CAP + 1, sizeof(Sample)));
  if (!reportHistory) logEvent("ATTENZIONE", "Memoria curva report non disponibile; riepilogo conservato senza grafico");
  if (prefsReady && !logs::makeRoom(logStore, persistedSeq, PERSIST_CAP, logs::kReserveEntries))
    Serial.println("[ATTENZIONE] NVS quasi piena anche dopo la pulizia dei vecchi log full");
  testInterrupted = prefs.getBool("testActive", false); prefs.putBool("testActive", false);
  bool wasActive = prefs.getBool("active", false);
  hadInterrupted = wasActive;
  prefs.putBool("active", false);
  previousCheckpoint = prefs.getULong64("checkpoint", 0);
  bootCount = prefs.getUInt("boots", 0) + 1; prefs.putUInt("boots", bootCount);
  resetReason = String(esp_reset_reason());
  if (wasActive) {
    lastInterruption.resetReason = esp_reset_reason();
    lastInterruption.checkpointUtc = previousCheckpoint;
    interruptionRecorded = true;
    prefs.putBytes("interruption", &lastInterruption, sizeof(lastInterruption));
  } else if (prefs.getBytesLength("interruption") == sizeof(lastInterruption) &&
             prefs.getBytes("interruption", &lastInterruption, sizeof(lastInterruption)) == sizeof(lastInterruption)) {
    interruptionRecorded = true;
  }
  phase = wasActive ? Phase::Interrupted : Phase::Idle;
  if (prefs.getBool("emergency", false)) {
    portENTER_CRITICAL(&heaterMux); outputs.emergencyStop(); writeOutputs(); portEXIT_CRITICAL(&heaterMux);
    phase = Phase::Fault; faultText = "EMERGENZA registrata: uscite bloccate fino al riconoscimento";
  }
  logEvent(wasActive ? "ATTENZIONE" : "INFO", wasActive ?
           "Ciclo interrotto da spegnimento/reset: uscita spenta, riavvio solo manuale" :
           "ESP avviato; causa reset codice " + resetReason);
  if (testInterrupted) logEvent("STOP", "Test interrotto da reset/alimentazione: nessuna ripresa automatica");
  // Keep the one-hour diagnostic ring off the internal RAM used by Wi-Fi/JSON.
  testHistory = static_cast<TestSample *>(ps_malloc(sizeof(TestSample) * TEST_HISTORY_CAP));
  testHistoryCapacity = testHistory ? TEST_HISTORY_CAP : TEST_HISTORY_PAGE;
  if (!testHistory) testHistory = static_cast<TestSample *>(calloc(testHistoryCapacity, sizeof(TestSample)));
  if (!testHistory) testHistoryCapacity = 0;
  if (testHistoryCapacity != TEST_HISTORY_CAP) logEvent("ATTENZIONE", "Storico TEST ridotto: PSRAM non disponibile; campioni " + String(testHistoryCapacity));
  loadRecipes();
  ControlSettings storedSettings{};
  if (prefs.isKey("control") && prefs.getBytesLength("control") == sizeof(storedSettings) &&
      prefs.getBytes("control", &storedSettings, sizeof(storedSettings)) == sizeof(storedSettings) &&
      heater::validSettings(storedSettings.ssr, storedSettings.maxPower, storedSettings.windowMs)) controlSettings = storedSettings;
  logEvent("COMANDO", String(controlSettings.ssr ? "SSR" : "Rele meccanico") + "; finestra " +
           String(controlSettings.windowMs / 1000) + " s; limite potenza " + String(controlSettings.maxPower, 1) + "%");
  PidGains storedGains{};
  if (prefs.isKey("pid") && prefs.getBytesLength("pid") == sizeof(storedGains) &&
      prefs.getBytes("pid", &storedGains, sizeof(storedGains)) == sizeof(storedGains)) {
    kp = storedGains.kp; ki = storedGains.ki; kd = storedGains.kd;
  }
  if (!std::isfinite(kp) || kp < 0 || kp > 50 || !std::isfinite(ki) || ki < 0 || ki > 0.5f ||
      !std::isfinite(kd) || kd < 0 || kd > 200) {
    kp = 12.0f; ki = 0.015f; kd = 50.0f;
    logEvent("ATTENZIONE", "Coefficienti PID salvati non validi: ripristinati valori iniziali");
  }
  pinMode(CS1, OUTPUT); digitalWrite(CS1, HIGH);
  pinMode(CS2, OUTPUT); digitalWrite(CS2, HIGH);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
  bool firstReady = rtd1.begin(MAX31865_3WIRE);
  bool secondReady = rtd2.begin(MAX31865_3WIRE);
  if (!firstReady || !secondReady)
    logEvent("ATTENZIONE", "Inizializzazione SPI MAX31865 fallita; verifica le due Click");
  rtd1.enable50Hz(true); rtd2.enable50Hz(true);
  sampleAndControl(); recordHistory(true);
  // Credentials come from the firmware; writing them again into the shared
  // NVS can fail when persistent event logs have filled that partition.
  // This must precede mode(), which initializes the Wi-Fi driver.
  WiFi.persistent(false);
  bool wifiReady = WiFi.mode(WORKSHOP_WIFI_SSID[0] ? WIFI_AP_STA : WIFI_AP);
  if (!wifiReady) logEvent("ALLARME", "Driver Wi-Fi non avviato");
  apReady = wifiReady && WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (!apReady) logEvent("ALLARME", "Rete Wi-Fi diretta non avviata");
  if (wifiReady && WORKSHOP_WIFI_SSID[0]) {
    WiFi.setAutoReconnect(true); WiFi.begin(WORKSHOP_WIFI_SSID, WORKSHOP_WIFI_PASSWORD);
    logEvent("RETE", "Connessione Wi-Fi capannone in corso");
  }
  setupOta();
  server.on("/api/report", HTTP_GET, reportApi);
  server.on("/api/report/history", HTTP_GET, reportHistoryApi);
  server.on("/api/test/command", HTTP_POST, testCommandApi);
  server.on("/api/test/heartbeat", HTTP_POST, testHeartbeatApi);
  server.on("/api/test/history", HTTP_GET, testHistoryApi);
  server.on("/", HTTP_GET, [] { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html; charset=utf-8", WEB_HTML); });
  server.on("/style.css", HTTP_GET, [] { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/css; charset=utf-8", WEB_CSS); });
  server.on("/app.js", HTTP_GET, [] { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "application/javascript; charset=utf-8", WEB_JS); });
  server.on("/report-export.js", HTTP_GET, [] { server.send_P(200, "application/javascript; charset=utf-8", WEB_EXPORT_JS); });
  server.on("/jspdf.umd.min.js", HTTP_GET, [] {
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "application/javascript", reinterpret_cast<const char *>(WEB_PDF_JS), sizeof(WEB_PDF_JS));
  });
  server.on("/api/status", HTTP_GET, [] { statusApi(); });
  server.on("/api/recipes", HTTP_GET, recipesApi);
  server.on("/api/recipes", HTTP_POST, saveRecipesApi);
  server.on("/api/command", HTTP_POST, commandApi);
  server.on("/api/pid", HTTP_POST, pidApi);
  server.on("/api/control", HTTP_POST, controlSettingsApi);
  server.on("/api/logs", HTTP_GET, logsApi);
  server.on("/api/history", HTTP_GET, historyApi);
  server.onNotFound([] { error(404, "Percorso non trovato"); });
  server.begin();
  chipTemperatureC = temperatureRead();
  lastChipTemperatureAt = millis();
  if (apReady) logEvent("RETE", "Dashboard diretta: http://" + WiFi.softAPIP().toString() + "/");
}
void loop() {
  drainHeaterNotices();
  if (otaEnabled) ArduinoOTA.handle();
  if (outputSnapshot().updating) { delay(5); return; }
  server.handleClient();
  updateNetwork();
  uint32_t now = millis();
  if ((phase == Phase::Running || phase == Phase::Paused) && uint32_t(now - sampleAtMs) >= heater::Guard::kStaleMs)
    trip("Lettura sonde troppo vecchia: uscita spenta");
  if (uint32_t(now - lastSampleAt) >= SAMPLE_MS) {
    lastSampleAt = now;
    sampleAndControl(); recordHistory(); recordTestHistory();
  }
  applyWindow();
  if ((phase == Phase::Running || phase == Phase::Paused) && ntpSynced &&
      uint32_t(now - checkpointAt) >= CHECKPOINT_MS) {
    uint64_t current = utcNow();
    if (current) {
      if (prefs.putULong64("checkpoint", current) == sizeof(uint64_t)) {
        checkpointAt = now; checkpointWriteFaultLogged = false;
      } else if (!checkpointWriteFaultLogged) {
        checkpointWriteFaultLogged = true;
        logEvent("ATTENZIONE", "Checkpoint ora non salvato: durata interruzione potrebbe essere sconosciuta");
      }
    }
  }
  if (uint32_t(now - lastChipTemperatureAt) >= 10000) {
    chipTemperatureC = temperatureRead();
    lastChipTemperatureAt = now;
  }
  portENTER_CRITICAL(&heaterMux); outputAlarm = phase == Phase::Fault; portEXIT_CRITICAL(&heaterMux);
  delay(10);
}
