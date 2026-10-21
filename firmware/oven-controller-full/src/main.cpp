#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include "control_web_server.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include "async_rtd.h"
#include <esp_system.h>
#include <esp_sntp.h>
#include <nvs.h>
#include <cmath>
#include <ctime>
#include <atomic>
#include "profile.h"
#include "pid_control.h"
#include "pause_control.h"
#include "fault_resume.h"
#include "manual_control.h"
#include "cycle_report.h"
#include "report_curve.h"
#include "output_supervisor.h"
#include "log_storage.h"
#include "sensor_recovery.h"
#include "thermal_limits.h"
#include "telegram_service.h"
#include "power_recovery.h"
#include "wifi_recovery.h"
#include "hardware_config.h"
#include "firmware_version.h"
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
#if __has_include("telegram_config.h")
#include "telegram_config.h"
#endif
#ifndef OVEN_TELEGRAM_TOKEN
#define OVEN_TELEGRAM_TOKEN ""
#endif
#ifndef OVEN_TELEGRAM_CHAT_ID
#define OVEN_TELEGRAM_CHAT_ID ""
#endif
TelegramService telegramBot;
telegram::Users telegramUsers;
telegram::PendingUsers telegramPending;
// ESP32-S3 N16R8; GPIO35/36 are occupied by PSRAM on this module.
constexpr int CS1 = PIN_CS1, CS2 = PIN_CS2;
constexpr int HEAT = PIN_RELAY, RED = PIN_RED, GREEN = PIN_GREEN;
constexpr uint32_t SAMPLE_MS = 500, HISTORY_MS = 10000;
constexpr size_t MAX_RECIPES = 8, MAX_STEPS = 12, HISTORY_CAP = 4320, HISTORY_PAGE = 180;
constexpr size_t EVENT_CAP = 128, PERSIST_CAP = 40;
constexpr char AP_SSID[] = "OvenController-Full";
constexpr char AP_PASSWORD[] = "oven12345";
constexpr char MDNS_NAME[] = "oven-full";
constexpr uint16_t OTA_PORT = 3232;
constexpr size_t TEST_HISTORY_CAP = 3600, TEST_HISTORY_PAGE = 300;

AsyncRtd rtd1(CS1), rtd2(CS2);
ControlWebServer server(80);
Preferences prefs;
bool prefsReady = false, eventWriteFaultLogged = false;
struct RecoveryStore {
  int load(recovery::Record &record) {
    if (!prefsReady) return -1;
    if (!prefs.isKey("cycleSafety")) return 0;
    if (prefs.getBytesLength("cycleSafety") != sizeof(record)) return -1;
    return prefs.getBytes("cycleSafety", &record, sizeof(record)) == sizeof(record) ? 1 : -1;
  }
  bool save(const recovery::Record &record) {
    return prefsReady && prefs.putBytes("cycleSafety", &record, sizeof(record)) == sizeof(record);
  }
} recoveryStore;
recovery::Journal recoveryJournal;

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
thermal::ProtectionSettings protectionSettings;
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
probes::PairRecovery sensorRecovery;
Recipe recipes[MAX_RECIPES], activeRecipe;
size_t recipeCount = 0;
Phase phase = Phase::Idle;
bool cycleStopped = false;
bool relayOn = false, apReady = false, ntpStarted = false, ntpSynced = false;
bool mdnsReady = false, stationWasConnected = false, sensorFaultLogged = false;
std::atomic<bool> wifiRecoveryRunning{false}, wifiRecoveryAllowed{true};
std::atomic<uint32_t> wifiRetryCount{0}, wifiRetryWaitMs{0}, wifiReasonSeq{0};
std::atomic<unsigned> wifiLastReason{0};
std::atomic<bool> wifiLastRetryAccepted{true};
uint32_t wifiLoggedReasonSeq = 0, wifiLoggedRetryCount = 0, lastNtpAttempt = 0;
bool hadInterrupted = false, checkpointWriteFaultLogged = false;
bool interruptionAlreadyPending = false, bootNoticePending = false;
recovery::Cause bootCause = recovery::Cause::Other;
struct TestSample { uint32_t seq, ms; float t1, t2; uint16_t raw1, raw2; uint8_t fault1, fault2, flags; };
TestSample *testHistory = nullptr;
size_t testHistoryCapacity = 0;
size_t testHistoryHead = 0, testHistoryCount = 0;
uint32_t testHistorySeq = 0, lastTestHistoryAt = 0;
uint32_t testRunSeq = 0, testStartedAtMs = 0;
float chipTemperatureC = NAN;
uint32_t lastChipTemperatureAt = 0;
String stationIp, faultText, resetReason, lastCycleError;
String interruptedProgram;
float setpointC = NAN, duty = 0;
regulation::Pid pidController;
regulation::ProgramPause pauseHold;
regulation::FaultResume faultResume;
manual::Session manualSession;
bool manualOperation = false;
float kp = 12.0f, ki = 0.015f, kd = 50.0f;
float stepStartC = NAN;
uint8_t stepIndex = 0;
uint32_t stepStarted = 0, cycleStarted = 0, pausedAt = 0, holdInBandMs = 0;
uint32_t stepElapsedBefore = 0;
uint32_t windowStarted = 0, lastSampleAt = 0, lastHistoryAt = 0;
uint32_t lastNetworkCheck = 0, lastPidLog = 0, lastLagLog = 0;
recovery::CheckpointSchedule checkpoints;
uint32_t sampleAtMs = 0, bootCount = 0, eventSeq = 0, persistedSeq = 0;
uint64_t previousCheckpoint = 0, outageUpperBoundSec = 0;
struct InterruptionRecord { uint32_t resetReason = 0; uint64_t checkpointUtc = 0, rebootUtc = 0; };
InterruptionRecord lastInterruption;
bool interruptionRecorded = false;
Event events[EVENT_CAP]; size_t eventHead = 0, eventCount = 0;
reports::Recorder cycleReport;
reports::Snapshot lastReport;
bool reportAvailable = false, reportSaved = false;
Sample *history = nullptr;
size_t historyCapacity = 0, historyHead = 0, historyCount = 0;
uint32_t historySeq = 0;
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
float meanC();
bool heatingOperation() {
  if (phase == Phase::Paused) return !manualOperation && pauseHold.active &&
    std::isfinite(pauseHold.targetC) && stepIndex < activeRecipe.count;
  if (phase != Phase::Running || !std::isfinite(setpointC)) return false;
  if (manualOperation) return manualSession.heatingAllowed() && (!manualSession.descending() || meanC() < setpointC);
  return stepIndex < activeRecipe.count &&
    (activeRecipe.steps[stepIndex].type != profile::Type::Cooldown || meanC() < setpointC);
}
const profile::Step *followingStep() {
  return stepIndex + 1 < activeRecipe.count ? &activeRecipe.steps[stepIndex + 1] : nullptr;
}
bool workshopReady() { return WiFi.status() == WL_CONNECTED && uint32_t(WiFi.localIP()) != 0; }
void wifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    const unsigned reason = info.wifi_sta_disconnected.reason ? info.wifi_sta_disconnected.reason : 1;
    // Reconnect explicitly requests a local disconnect (reason 8). Retain
    // the original failure rather than replacing it with our recovery action.
    if (reason != 8) { wifiLastReason = reason; ++wifiReasonSeq; }
  }
}
void wifiRecoveryTask(void *) {
  network::RetryPlan plan; plan.begin(millis());
  for (;;) {
    const uint32_t now = millis(); const bool connected = workshopReady();
    const auto action = plan.tick(now, connected, wifiRecoveryAllowed.load());
    wifiRetryWaitMs = connected ? 0 : plan.waitMs(now);
    if (action != network::Action::None && !workshopReady() && wifiRecoveryAllowed.load()) {
      bool accepted;
      if (action == network::Action::Reconfigure || !(WiFi.getMode() & WIFI_MODE_STA)) {
        // Station only: keep the direct AP and keep credentials out of NVS.
        const auto status = WiFi.begin(WORKSHOP_WIFI_SSID, WORKSHOP_WIFI_PASSWORD, 0, nullptr, true);
        accepted = status != WL_CONNECT_FAILED && status != WL_NO_SHIELD;
      } else accepted = WiFi.reconnect();
      wifiLastRetryAccepted = accepted; ++wifiRetryCount;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
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
  duty = 0; pidController.reset(); windowStarted = millis();
  inhibitHeater();
}
void recordHistory(bool force = false, uint8_t eventFlag = 0);
void finishCycleReport(Phase next, const String &reason);
String telegramStatus(int64_t recipient = 0);
void telegramNotice(const String &title, const String &reason = "", bool critical = false,
                    telegram::EventScope scope = telegram::EventScope::Selected);
void stopCycle(Phase next, String reason) {
  const float finalReference = setpointC;
  const auto output = outputSnapshot();
  if (next != Phase::Fault || output.emergency || output.recoveryBlocked || output.updating || output.diagnostic)
    faultResume.clear();
  else if (cycleActive()) {
    if (manualOperation) faultResume.capture(manualSession, millis());
    else if (stepIndex < activeRecipe.count)
      faultResume.capture(activeRecipe.steps[stepIndex], stepIndex, phase == Phase::Paused, pauseHold,
        stepStartC, stepStarted, stepElapsedBefore, holdInBandMs, millis());
  }
  pauseHold.clear();
  resetPid();
  if (next == Phase::Fault) faultText = reason;
  if (manualOperation && cycleActive()) {
    manualSession.stop(millis()); phase = next; setpointC = NAN; cycleStopped = false;
    if (next == Phase::Fault) { lastCycleError = reason.substring(0, 240); prefs.putString("cycleError", lastCycleError); }
    const bool saved = recoveryJournal.finish(recoveryStore);
    const bool legacySaved = prefs.putBool("active", false) == sizeof(bool);
    if (!saved || !legacySaved) {
      portENTER_CRITICAL(&heaterMux); outputs.blockRecovery(); writeOutputs(); portEXIT_CRITICAL(&heaterMux);
      recoveryJournal.data.pending = true; phase = Phase::Fault;
      next = Phase::Fault;
      faultResume.clear();
      faultText = "Registro di sicurezza non salvato: uscite bloccate; riconoscimento richiesto";
      reason = faultText;
      lastCycleError = faultText;
      logEvent("ALLARME", faultText, false); telegramNotice("ALLARME MEMORIA DI SICUREZZA", faultText, true);
    }
    logEvent(next == Phase::Fault ? "ALLARME" : "MANUALE", reason);
    recordHistory(true, next == Phase::Fault ? HISTORY_FAULT : HISTORY_STOP);
    if (!outputSnapshot().emergency && saved && legacySaved)
      telegramNotice(next == Phase::Fault ? "ALLARME · CONTROLLO MANUALE" : "MANUALE FERMATO · STOP", reason, next == Phase::Fault);
    return; // Manual operation never creates/replaces a program report.
  }
  if (phase == Phase::Running || phase == Phase::Paused) {
    cycleStopped = next == Phase::Idle;
    if (next == Phase::Fault) {
      lastCycleError = reason.substring(0, 240);
      // Best effort: the report and log also record the fault if NVS is full.
      prefs.putString("cycleError", lastCycleError);
    }
    phase = next;
    setpointC = NAN;
    const bool safetySaved = recoveryJournal.finish(recoveryStore);
    const bool legacySaved = prefs.putBool("active", false) == sizeof(bool);
    if (!safetySaved || !legacySaved) {
      portENTER_CRITICAL(&heaterMux); outputs.blockRecovery(); writeOutputs(); portEXIT_CRITICAL(&heaterMux);
      recoveryJournal.data.pending = true;
      faultResume.clear();
      phase = Phase::Fault; faultText = "Registro di sicurezza non salvato: uscite bloccate; riconoscimento richiesto";
      // Report, marker and notification must describe the actual final state.
      next = Phase::Fault; reason = faultText; cycleStopped = false;
      lastCycleError = faultText;
      logEvent("ALLARME", faultText, false);
    }
    finishCycleReport(next, reason);
    if (reportHistory && reportAvailable) {
      reportHistoryCount = reports::copyCurve(history, historyCapacity, historyHead, historyCount,
        lastReport.startMs, lastReport.endMs, reportHistory, historyCapacity, reportHistoryPartial);
      const uint8_t flags = (sensors[0].valid ? 1 : 0) | (sensors[1].valid ? 2 : 0) |
                           (std::isfinite(finalReference) ? 8 : 0) |
                           (next == Phase::Complete ? HISTORY_COMPLETE : next == Phase::Fault ? HISTORY_FAULT : HISTORY_STOP);
      reportHistory[reportHistoryCount++] = {historySeq + 1, lastReport.endMs, sensors[0].c, sensors[1].c,
                                           finalReference, 0, flags, stepIndex};
    }
    logEvent(next == Phase::Fault ? "ALLARME" : "CICLO", reason);
    recordHistory(true, next == Phase::Complete ? HISTORY_COMPLETE :
                        next == Phase::Fault ? HISTORY_FAULT : HISTORY_STOP);
    if (!outputSnapshot().emergency)
      telegramNotice(next == Phase::Fault ? "ALLARME FORNO" : next == Phase::Complete ? "CICLO COMPLETATO" :
        next == Phase::Interrupted ? "CICLO INTERROTTO" : "CICLO FERMATO · STOP", reason,
        next == Phase::Fault || next == Phase::Interrupted);
    if (reportAvailable && telegramBot.configured &&
        !telegramBot.report(lastReport, reportHistory, reportHistoryCount, reportHistoryPartial))
      logEvent("ATTENZIONE", "PDF Telegram non accodato: memoria/coda non disponibili; report consultabile dalla dashboard", false);
  } else if (next == Phase::Fault && phase != Phase::Fault) {
    phase = Phase::Fault; logEvent("ALLARME", reason);
    faultText = reason;
    if (!outputSnapshot().emergency) telegramNotice("ALLARME FORNO", reason, true);
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
         "Piano di controllo non valido: uscita spenta");
  }
}
String sensorMessage(const Sensor &sensor);
struct SensorRead {
  bool previousValid = false, previousRecoveryFailed = false, done = false, checking = false, rawFault = false;
  uint8_t previousFault = 0, initialFault = 0;
  uint16_t initialRaw = 0;
  probes::RecoveryResult result = probes::RecoveryResult::NotAttempted;
};
SensorRead sensorReads[2];
bool sensorBatchActive = false, sensorBatchChecking = false;
void startSensorPair() {
  sensorBatchActive = true; sensorBatchChecking = false;
  sensorRecovery = probes::PairRecovery{};
  for (size_t i = 0; i < 2; ++i) {
    sensorReads[i] = SensorRead{};
    sensorReads[i].previousValid = sensors[i].valid; sensorReads[i].previousFault = sensors[i].fault;
    sensorReads[i].previousRecoveryFailed = sensors[i].recoveryFailed;
    sensors[i].recoveryFailed = false;
  }
  rtd1.start(millis()); rtd2.start(millis());
}
void finishSensorRead(size_t index) {
  auto &sensor = sensors[index]; const auto &read = sensorReads[index];
  sensor.recoveryFailed = read.result == probes::RecoveryResult::Failed;
  if (sensor.recoveryFailed) sensor.valid = false;
  if (sensor.recoveryFailed && diagnosticMode()) {
    stopTests("Fault PT100 persistente/non sicuro: TEST fermato", true);
    trip("TEST · PT100 #" + String(index + 1) + ": " + sensorMessage(sensor));
  }
  // A disconnected probe is still checked, but the same failed verification
  // at rest must not fill the log or write NVS every second.
  if (read.checking && (read.result == probes::RecoveryResult::Confirmed ||
      !read.previousRecoveryFailed || sensor.fault != read.previousFault))
    logEvent("SONDA", "PT100 #" + String(index + 1) + ": Fault 0x" + String(read.initialFault, HEX) +
      " · RAW " + String(read.initialRaw) +
      (read.result == probes::RecoveryResult::Confirmed ?
       " · transitorio recuperato con una nuova coppia valida; uscita spenta durante verifica" :
       " · verifica non confermata; arresto e allarme"));
  if (read.result == probes::RecoveryResult::Confirmed)
    telegramBot.notifyEvent("⚠️ AVVISO SONDA · PT100 #" + String(index + 1) +
      "\nLettura anomala recuperata · Fault 0x" + String(read.initialFault, HEX) + " · RAW " + String(read.initialRaw) +
      ". Nuova coppia valida; uscite spente durante la verifica.\nEvento a uptime " +
      String(millis() / 1000) + " s.", false, telegram::EventScope::DetailedOnly);
  if (sensor.valid != read.previousValid || sensor.fault != read.previousFault)
    logEvent(sensor.valid ? "PT100" : "SONDA", "PT100 #" + String(index + 1) + ": " + sensorMessage(sensor) +
      " · Fault 0x" + String(sensor.fault, HEX) + " · RAW " + String(sensor.raw) + " · " + String(sensor.ohms, 2) + " ohm");
}
bool pollSensorPair() {
  if (!sensorBatchActive) return false;
  AsyncRtd *devices[] = {&rtd1, &rtd2};
  for (size_t i = 0; i < 2; ++i) {
    auto &read = sensorReads[i]; auto &device = *devices[i]; auto &sensor = sensors[i];
    if (read.done || !device.tick(millis())) continue;
    sensor.raw = device.conversion.raw; sensor.fault = device.conversion.fault;
    read.rawFault = device.conversion.rawFault;
    const float reference = i == 0 ? RREF_1 : RREF_2;
    sensor.ohms = sensor.raw * reference / 32768.0f;
    sensor.c = device.calculateTemperature(sensor.raw, RNOMINAL, reference);
    sensor.valid = !read.rawFault && sensor.fault == 0 && sensor.raw > 0 && sensor.raw < 32767 && thermal::validReading(sensor.c, diagnosticMode());
    if (!sensor.valid && !read.checking) {
      read.initialFault = sensor.fault; read.initialRaw = sensor.raw;
      read.checking = true;
      if (!sensorBatchChecking) {
        sensorBatchChecking = true;
        portENTER_CRITICAL(&heaterMux); outputs.beginSensorCheck(); writeOutputs(); portEXIT_CRITICAL(&heaterMux);
        holdTimer.update(millis(), false, false);
      }
    }
    read.done = true;
  }
  if (!sensorReads[0].done || !sensorReads[1].done) return false;
  if (sensorBatchChecking) {
    const auto result = sensorRecovery.observe(
      probes::confirmation(sensors[0].fault, sensors[0].raw, sensors[0].c, diagnosticMode(), sensorReads[0].rawFault),
      probes::confirmation(sensors[1].fault, sensors[1].raw, sensors[1].c, diagnosticMode(), sensorReads[1].rawFault));
    if (result == probes::RecoveryResult::Pending) {
      // Reacquire BOTH probes in every round; do not retain a stale healthy sibling.
      for (size_t i = 0; i < 2; ++i) { sensorReads[i].done = false; devices[i]->start(millis()); }
      return false;
    }
    for (size_t i = 0; i < 2; ++i)
      if (sensorReads[i].checking || sensorRecovery.probe[i].failure != probes::RecoveryFailure::None)
        sensorReads[i].result = result;
  }
  // Keep verification inhibition through fault handling/logging. No new output
  // is published until both probes and relative protections have been checked.
  for (size_t i = 0; i < 2; ++i) finishSensorRead(i);
  if (sensorBatchChecking) {
    portENTER_CRITICAL(&heaterMux); outputs.endSensorCheck(); writeOutputs(); portEXIT_CRITICAL(&heaterMux);
  }
  sensorBatchActive = false; sampleAtMs = millis(); return true;
}
void readSensorPair() {
  // Explicit START/RESUME/RESET still require a fresh completed pair. This
  // bounded wait is used only by operator commands, never for periodic sampling.
  // The network and independent GPIO supervisor continue on their own tasks.
  if (!sensorBatchActive) startSensorPair();
  while (!pollSensorPair()) delay(1);
}
String sensorMessage(const Sensor &sensor) {
  if (sensor.valid) return "Lettura valida";
  String text;
  if (sensor.recoveryFailed) {
    const auto failure = sensorRecovery.probe[&sensor == &sensors[0] ? 0 : 1].failure;
    if (failure == probes::RecoveryFailure::ConsecutiveFaults)
      text += String(probes::Recovery::kMaxConsecutiveFaults) + " letture non valide consecutive: recupero fallito; ";
    else if (failure == probes::RecoveryFailure::Unstable)
      text += "Letture instabili: nessuna coppia valida ottenuta entro la verifica limitata; ";
    else text += "Conferma non valida o fuori dai limiti di sicurezza: recupero interrotto; ";
  }
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
String protectionProblem(float reference, bool cooling = false) {
  if (thermal::probeDeltaExceeded(sensors[0].c,sensors[1].c,protectionSettings))
    return "Differenza PT100: " + String(fabsf(sensors[0].c-sensors[1].c),2) + " °C (limite >" +
      String(protectionSettings.maxProbeDeltaC,1) + " °C): uscita spenta";
  if (thermal::meanOvershootExceeded(sensors[0].c,sensors[1].c,reference,protectionSettings,cooling))
    return "Sovratemperatura media PT100: " + String((sensors[0].c+sensors[1].c)*.5f,2) +
      " °C; setpoint " + String(reference,2) + " °C; scostamento +" + String((sensors[0].c+sensors[1].c)*.5f-reference,2) +
      " °C (limite >" + String(protectionSettings.maxMeanOvershootC,1) + " °C): uscita spenta";
  return "";
}
bool checkProtections(float reference, bool cooling = false) {
  const String problem = protectionProblem(reference,cooling);
  if (!problem.length()) return true;
  trip(problem); return false;
}
bool sensorsHealthy() {
  return thermal::healthyPair(sensors[0].c, sensors[0].valid, sensors[1].c, sensors[1].valid);
}
const char *faultResumeProblem() {
  const auto output = outputSnapshot();
  if (output.emergency) return "Emergenza: usa RESET & BLOCK e avvia un nuovo controllo.";
  if (output.recoveryBlocked || recoveryJournal.data.pending || phase == Phase::Interrupted)
    return "Blackout/riavvio o memoria di sicurezza: continuazione non disponibile.";
  if (output.updating || output.diagnostic) return "Esci da TEST e attendi la fine dell'aggiornamento.";
  if (output.sensorChecking) return "Verifica sonde in corso: attendi una nuova coppia valida.";
  if (phase != Phase::Fault || !faultResume.available)
    return "Nessun punto di ripresa in RAM. Dopo riavvio, STOP o RESET & BLOCK serve un nuovo avvio.";
  if (!faultResume.manual && (faultResume.step >= activeRecipe.count || !cycleReport.canContinue()))
    return "Stato del programma non disponibile: usa RESET & BLOCK.";
  if (!sensorsHealthy() || uint32_t(millis()-sampleAtMs) >= heater::Guard::kStaleMs)
    return "Ripristina entrambe le sonde e attendi letture valide e fresche sotto 415 °C.";
  if (!faultResume.safePair(sensors[0].c, sensors[0].valid, sensors[1].c, sensors[1].valid,protectionSettings))
    return "Attendi il rientro delle temperature nelle soglie abilitate in Parameters.";
  return "";
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
String telegramTime(uint32_t seconds) {
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", (unsigned long)(seconds / 3600),
    (unsigned long)((seconds / 60) % 60), (unsigned long)(seconds % 60));
  return buffer;
}
String telegramStatus(int64_t recipient) {
  const auto output = outputSnapshot();
  const uint32_t now = millis();
  const char *icon = "💤";
  const char *state = "IDLE · NESSUN CICLO ATTIVO";
  if (output.emergency) { icon = "🚨"; state = "EMERGENZA ATTIVA"; }
  else if (phase == Phase::Fault || faultText.length()) { icon = "🚨"; state = "IN ALLARME"; }
  else if (phase == Phase::Interrupted) {
    icon = "🚨";
    state = lastInterruption.resetReason == ESP_RST_POWERON || lastInterruption.resetReason == ESP_RST_BROWNOUT ?
      "BLACKOUT · CICLO BLOCCATO" : "RIAVVIO · CICLO BLOCCATO";
  }
  else if (output.updating) { icon = "⚠️"; state = "AGGIORNAMENTO IN CORSO"; }
  else if (output.diagnostic) { icon = "⚠️"; state = "MODALITÀ TEST"; }
  else if (!sensorsHealthy() || uint32_t(now - sampleAtMs) >= heater::Guard::kStaleMs) {
    icon = "⚠️"; state = "SONDE NON PRONTE";
  }
  else if (phase == Phase::Paused) { icon = "⏸️"; state = "CICLO IN PAUSA · MANTENIMENTO"; }
  else if (phase == Phase::Running && manualOperation) {
    icon = manualSession.mode == manual::Mode::Cooldown || manualSession.coolingToTarget ? "❄️" : "✅";
    state = manualSession.mode == manual::Mode::Cooldown ? (manualSession.cooldownComplete ? "MANUALE · COOLDOWN COMPLETATO" : "MANUALE · COOLDOWN REGOLATO") :
      manualSession.coolingToTarget ? "MANUALE · DISCESA REGOLATA" : "MANUALE · REGOLAZIONE";
  }
  else if (phase == Phase::Running) { icon = "✅"; state = "CICLO IN CORSO"; }
  else if (phase == Phase::Complete) { icon = "✅✅✅"; state = "CICLO COMPLETATO"; }
  else if (cycleStopped) {
    icon = "⚠️"; state = "STOP · CICLO FERMATO";
  }
  String text = "Stato: " + String(icon) + " " + state + "\n";
  const time_t epoch = time(nullptr);
  if (epoch >= 1700000000) {
    char formatted[64]; reportpdf::date(uint64_t(epoch), formatted, sizeof(formatted));
    text += "Aggiornato: " + String(formatted) + "\n";
  }
  if (output.emergency) text += "\n🚨 EMERGENZA ATTIVA\nRiconoscimento dalla dashboard richiesto.\n";
  if (faultText.length()) text += "Motivo: " + faultText.substring(0, 300) + "\n";
  if (phase == Phase::Fault && faultResume.available)
    text += String("\n⏯️ RIPRESA DALLA DASHBOARD\n") + (faultResumeProblem()[0] ? faultResumeProblem() :
      "RESET & CONTINUE disponibile: riprende il controllo interrotto. RESET & BLOCK lo lascia fermo.") + "\n";
  if (output.diagnostic) text += "\n🧪 TEST\n" + String(testModeName(output.test.mode)) + "\n";
  // Selection and historical reports do not represent a currently active cycle.
  if (cycleActive() && manualOperation) {
    text += "\n🎛 CONTROLLO MANUALE\n";
    if (manualSession.mode == manual::Mode::Cooldown) text += manualSession.cooldownComplete ? "Target cooldown raggiunto: riscaldamento SPENTO, monitoraggio attivo.\n" : "Cooldown regolato: PID sotto il setpoint; raffreddamento naturale sopra il setpoint.\n";
    text += "Target finale: " + String(manualSession.target, 1) + " °C\n";
    text += "Setpoint: " + String(manualSession.reference, 1) + " °C · rate " + String(manualSession.rateCMin, 2) + " °C/min\n";
    if (manualSession.mode != manual::Mode::Cooldown) text +=
      String(manualSession.coolingToTarget ? "Discesa regolata: calore solo sotto il setpoint.\n" : "Regolazione PID fino al comando dell'operatore.\n");
    text += "Tempo manuale: " + telegramTime(uint32_t(manualSession.elapsedMs(now) / 1000)) + "\n";
  } else if (cycleActive()) {
    text += "\n📋 CICLO ATTIVO\nProgramma: " + activeRecipe.name + "\n";
    String remainingText = "non disponibile";
    bool estimated = false;
    if (stepIndex < activeRecipe.count) {
      const auto &step = activeRecipe.steps[stepIndex];
      const char *kind = step.type == profile::Type::Ramp ? "Rampa" : step.type == profile::Type::Hold ? "Mantenimento" : "Cooldown regolato";
      text += "Step: " + String(stepIndex + 1) + "/" + String(activeRecipe.count) + " · " + kind +
        "\nTarget finale: " + String(step.target, 1) + " °C\n";
      if (step.type != profile::Type::Hold) text += "Rate: " + String(step.rate, 2) + " °C/min\n";
      else text += "Hold ±5 °C: " + telegramTime(holdInBandMs / 1000) + " / " + telegramTime(uint32_t(step.minutes) * 60) + "\n";
      const auto elapsed = phase == Phase::Paused ? 0 : uint32_t(now - stepStarted);
      const float origin = phase == Phase::Paused ? pauseHold.targetC : stepStartC;
      const float remaining = profile::remainingSec(step, origin, elapsed, holdInBandMs,
        sensors[0].valid && sensors[1].valid ? fminf(sensors[0].c, sensors[1].c) : NAN,
        sensors[0].valid && sensors[1].valid ? hottestC() : NAN, followingStep());
      if (std::isfinite(remaining)) remainingText = telegramTime(uint32_t(remaining));
      estimated = step.type != profile::Type::Hold;
    }
    text += "Target istantaneo: " + (std::isfinite(setpointC) ? String(setpointC, 2) + " °C" : String("non disponibile")) + "\n";
    if (phase == Phase::Paused) text += "PID mantiene la temperatura di pausa; avanzamento e tempo dello step sospesi.\n";
    text += "\n⏱ TEMPI\nTempo ciclo totale: " + telegramTime(uint32_t(now - cycleStarted) / 1000) + "\n";
    text += "Rimanente step" + String(estimated ? " (stima): " : ": ") + remainingText + "\n";
  }
  text += "\n🌡 TEMPERATURE\n";
  for (size_t i = 0; i < 2; ++i) {
    text += "PT100 #" + String(i + 1) + ": ";
    if (sensors[i].valid) text += String(sensors[i].c, 2) + " °C";
    else text += "NON VALIDA · fault 0x" + String(sensors[i].fault, HEX) + " · RAW " + String(sensors[i].raw);
    text += "\n";
  }
  if (sensors[0].valid && sensors[1].valid)
    text += "Media: " + String(meanC(), 2) + " °C\nDifferenza sonde: " + String(fabsf(sensors[0].c - sensors[1].c), 2) + " °C\n";
  text += "Età letture: " + String(uint32_t(now - sampleAtMs)) + " ms" +
    (uint32_t(now - sampleAtMs) >= heater::Guard::kStaleMs ? " · ⚠️ SCADUTE" : "") + "\n";
  text += "\n⚡ USCITA\nComando GPIO: " + String(output.relay ? "ON" : "OFF") + "\nComando PID: " + String(duty, 1) + "%\n";
  text += "\n⚙️ REGOLAZIONE\nAttuatore: " + String(controlSettings.ssr ? "SSR" : "Relè") + "\nFinestra: " + String(controlSettings.windowMs / 1000) +
    " s · limite: " + String(controlSettings.maxPower, 1) + "%\n";
  text += "PID: Kp " + String(kp, 3) + " · Ki " + String(ki, 4) + " · Kd " + String(kd, 2) + "\n";
  text += "\n🛡 BLOCCHI TERMICI\nDelta sonde: " + String(protectionSettings.probeDeltaEnabled ? "ON >" : "OFF · soglia ") +
    String(protectionSettings.maxProbeDeltaC,1) + " °C\nMedia − target: " + (protectionSettings.meanOvershootEnabled ? "ON >" : "OFF · soglia ") +
    String(protectionSettings.maxMeanOvershootC,1) + " °C\n";
  if ((lastCycleError.length() && lastCycleError != faultText) || interruptionRecorded)
    text += "\n⚠️ ERRORI REGISTRATI\n";
  if (lastCycleError.length() && lastCycleError != faultText) text += "Ultimo errore di ciclo: " + lastCycleError.substring(0, 240) + "\n";
  if (interruptionRecorded) {
    const bool power = lastInterruption.resetReason == ESP_RST_POWERON || lastInterruption.resetReason == ESP_RST_BROWNOUT;
    const bool pending = recoveryJournal.data.pending || output.recoveryBlocked;
    text += String(pending ? "🚨 " : "ℹ️ Storico · ") + (power ? "BLACKOUT / alimentazione ESP" : "Interruzione da riavvio") +
      " · reset " + String(lastInterruption.resetReason);
    if (interruptedProgram.length()) text += "\nProgramma interrotto: " + interruptedProgram;
    if (lastInterruption.checkpointUtc && lastInterruption.rebootUtc >= lastInterruption.checkpointUtc)
      text += "\nIntervallo fino al riavvio (max stimato): " + telegramTime(uint32_t(lastInterruption.rebootUtc - lastInterruption.checkpointUtc));
    else text += "\nIntervallo massimo: durata sconosciuta";
    text += pending ? "\nInterruzione da riconoscere: premi RESET & BLOCK.\n" :
      "\nInterruzione già riconosciuta: nessun blocco da questo evento.\n";
  }
  text += "\n🧠 SISTEMA\nUptime: " + telegramTime(now / 1000) + "\nAvvio: #" + String(bootCount) + "\n";
  text += "RAM libera/min: " + String(ESP.getFreeHeap()) + " / " + String(ESP.getMinFreeHeap()) + " B\n";
  text += "Modalità notifiche: " + String(telegram::modeName(telegramBot.mode(recipient))) + "\n";
  text += "Wi-Fi: " + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) + " dBm";
  return text;
}
void telegramNotice(const String &title, const String &reason, bool critical, telegram::EventScope scope) {
  if (!telegramBot.configured) return;
  const auto recipients=telegramBot.userSettings();
  for(const auto &user:recipients.entries){
  if(user.chat<=0)continue;
  const auto mode = telegram::modeFrom(user.mode);
  if (!telegram::allowEvent(mode, critical, scope)) continue;
  const char *icon = critical ? "🚨" : "✅";
  if (title.indexOf("EMERGENZA") >= 0 || title.indexOf("ALLARME") >= 0) icon = "🚨";
  else if (title == "CICLO AVVIATO" || title == "MANUALE AVVIATO") icon = "🚀";
  else if (title == "MANUALE · TARGET MODIFICATO") icon = "🎯";
  else if (title == "MANUALE · COOLDOWN") icon = "❄️";
  else if (title == "CICLO IN PAUSA") icon = "⏸️";
  else if (title == "CICLO RIPRESO" || title.startsWith("MANUALE RIPRESO")) icon = "⏯️";
  else if (title.indexOf("STOP") >= 0 || title.indexOf("FERMATO") >= 0 || title.indexOf("PAUSA") >= 0 ||
           title.indexOf("INTERROT") >= 0 || title.indexOf("INTERRUZ") >= 0 || title.indexOf("RIAVVI") >= 0 ||
           title.indexOf("AVVISO") >= 0) icon = "⚠️";
  String text = String(icon) + " " + title + "\n";
  char date[64]; reportpdf::date(utcNow(), date, sizeof(date));
  if (!critical && mode != telegram::Mode::Detailed) {
    const bool cycleEvent = title == "CICLO AVVIATO" || title == "CICLO FERMATO · STOP" ||
      title == "CICLO COMPLETATO" || title == "CICLO IN PAUSA" || title == "CICLO RIPRESO";
    if (cycleEvent && activeRecipe.count) text += "Programma: " + activeRecipe.name + "\n";
    if (title.startsWith("MANUALE") && reason.length()) text += reason + "\n";
    if (title == "ESP AVVIATO") text += String("Avvio completato · firmware ") + firmwareinfo::kVersion +
      "\nWi-Fi capannone connesso: " + WiFi.localIP().toString() + "\n";
    text += "📅 " + String(date);
    telegramBot.notifyEvent(text, false, scope,user.chat);
    continue;
  }
  text += "📅 " + String(date) + "\n";
  if (!cycleActive() && activeRecipe.count) text += "\nProgramma dell'evento: " + activeRecipe.name + "\n";
  if (reason.length()) text += "Motivo: " + reason + "\n";
  text += "\n📍 STATO AL MOMENTO DELL'EVENTO\n" + telegramStatus(user.chat);
  telegramBot.notifyEvent(text, critical, scope,user.chat);
  }
}
void beginStep(bool preservePid = false) {
  if (stepIndex >= activeRecipe.count) {
    stopCycle(Phase::Complete, "Ciclo finito: tutti gli step completati"); return;
  }
  stepStarted = millis();
  stepElapsedBefore = 0; pauseHold.clear();
  const auto &step = activeRecipe.steps[stepIndex];
  stepStartC = profile::startTemperature(step, meanC(), stepIndex > 0 ? setpointC : NAN);
  holdInBandMs = 0;
  holdTimer.reset(stepStarted);
  if (!preservePid) resetPid();
  setpointC = profile::setpoint(step, stepStartC, 0);
  logEvent("CICLO", "Step " + String(stepIndex + 1) + "/" + String(activeRecipe.count) +
          " " + String(typeName(step.type)) + " verso " + String(step.target, 1) + " °C" +
          (step.type == profile::Type::Hold ? "; " + String(step.minutes) + " min in banda" :
           "; " + String(step.rate, 2) + " °C/min"));
}
void advanceStep() {
  const bool preservePid = stepIndex + 1 < activeRecipe.count &&
    regulation::continuousControl(activeRecipe.steps[stepIndex], activeRecipe.steps[stepIndex + 1]);
  cycleReport.advance(millis(), holdInBandMs); ++stepIndex; beginStep(preservePid);
}
void applyWindow() {
  heater::Plan plan;
  plan.cycleActive = phase == Phase::Running || phase == Phase::Paused;
  plan.heatingAllowed = heatingOperation();
  plan.healthy = sensorsHealthy(); plan.sampleAt = sampleAtMs;
  plan.windowAt = windowStarted;
  plan.windowMs = controlSettings.windowMs; plan.duty = duty;
  portENTER_CRITICAL(&heaterMux); outputs.publish(plan); portEXIT_CRITICAL(&heaterMux);
}
void controlPid(float actual, uint32_t now, bool descending = false) {
  duty = pidController.update(setpointC, actual, kp, ki, kd, controlSettings.maxPower, now, descending);
  applyWindow();
  if (uint32_t(now - lastPidLog) >= 30000) {
    lastPidLog = now;
    logEvent("PID", "Media " + String(actual, 1) + " °C; setpoint " + String(setpointC, 1) +
             " °C; uscita " + String(duty, 0) + "%", false);
  }
}
void sampleAndControl(bool fresh = true) {
  if (fresh) readSensorPair();
  uint32_t now = millis();
  if (!manualOperation && cycleActive() && stepIndex < activeRecipe.count) {
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
  if (phase == Phase::Paused) {
    holdTimer.update(now, false, false);
    if (!pauseHold.active || !thermal::validTarget(pauseHold.targetC)) {
      trip("Target di pausa non valido: uscita spenta"); return;
    }
    setpointC = pauseHold.targetC;
    if (!checkProtections(setpointC)) return;
    controlPid(meanC(), now); return;
  }
  if (phase != Phase::Running) { holdTimer.update(now, false, false); inhibitHeater(); return; }
  if (manualOperation) {
    manualSession.tick(hottestC(), now);
    setpointC = manualSession.reference;
    if (!checkProtections(setpointC,manualSession.descending())) return;
    if (!manualSession.heatingAllowed()) { duty = 0; inhibitHeater(); return; }
    controlPid(meanC(), now, manualSession.descending()); return;
  }
  const auto &step = activeRecipe.steps[stepIndex];
  float actual = meanC();
  uint32_t elapsed = uint32_t(now - stepStarted);
  setpointC = profile::setpoint(step, stepStartC, elapsed);
  if (!checkProtections(setpointC,step.type == profile::Type::Cooldown)) return;
  if (step.type == profile::Type::Cooldown) {
    if (profile::cooldownDone(step, stepStartC, elapsed, hottestC())) { advanceStep(); return; }
    controlPid(actual, now, true);
    return;
  }
  controlPid(actual, now);
  if (step.type == profile::Type::Ramp) {
    if (profile::rampDone(step, stepStartC, elapsed, fminf(sensors[0].c, sensors[1].c), hottestC(), followingStep())) {
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
  if (!historyCapacity) return;
  uint32_t now = millis();
  relayOn = heaterOn();
  if (!force && uint32_t(now - lastHistoryAt) < HISTORY_MS) return;
  lastHistoryAt = now;
  uint8_t flags = (sensors[0].valid ? 1 : 0) | (sensors[1].valid ? 2 : 0) |
                  (relayOn ? 4 : 0) | (std::isfinite(setpointC) ? 8 : 0) | eventFlag;
  history[historyHead] = {++historySeq, now, sensors[0].valid ? sensors[0].c : 0,
                          sensors[1].valid ? sensors[1].c : 0,
                          std::isfinite(setpointC) ? setpointC : 0, duty, flags, uint8_t(manualOperation ? 255 : stepIndex)};
  historyHead = (historyHead + 1) % historyCapacity;
  if (historyCount < historyCapacity) ++historyCount;
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
  if (!reports::supportedVersion(candidate.version) || !candidate.stepCount || candidate.stepCount > reports::kMaxSteps ||
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
  doc["version"] = report.version; doc["firmware"] = firmwareinfo::kVersion; doc["saved"] = reportSaved;
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
  bool stationReady = workshopReady();
  doc["mode"] = "FULL"; doc["firmware"] = firmwareinfo::kVersion;
  doc["firmwareModifiedOn"] = firmwareinfo::kModifiedOn;
  doc["phase"] = phaseName(); doc["relay"] = heaterOn();
  doc["cooldownRegulated"] = true;
  doc["pauseMaintainsTemperature"] = phase == Phase::Paused && pauseHold.active;
  if (phase == Phase::Paused && pauseHold.active) doc["pauseTargetC"] = pauseHold.targetC;
  else doc["pauseTargetC"] = nullptr;
  doc["operation"] = manualOperation ? "manual" : "program";
  JsonObject manualState = doc["manual"].to<JsonObject>();
  manualState["active"] = manualOperation && cycleActive() && manualSession.active();
  manualState["mode"] = manual::modeName(manualSession.mode);
  manualState["coolingToTarget"] = manualSession.coolingToTarget;
  manualState["cooldownComplete"] = manualSession.cooldownComplete;
  if (std::isfinite(manualSession.target)) manualState["targetC"] = manualSession.target; else manualState["targetC"] = nullptr;
  manualState["elapsedSec"] = manualSession.elapsedMs(millis()) / 1000;
  manualState["startedAtMs"] = manualSession.startedAt;
  manualState["rateCMin"] = manualSession.rateCMin;
  if (std::isfinite(manualSession.reference)) manualState["setpointC"] = manualSession.reference; else manualState["setpointC"] = nullptr;
  doc["emergency"] = output.emergency;
  doc["resetRequired"] = phase == Phase::Fault || phase == Phase::Interrupted || output.emergency ||
    output.recoveryBlocked || recoveryJournal.data.pending || output.guard.fault != heater::Fault::None;
  doc["supervisorFault"] = static_cast<unsigned>(output.guard.fault);
  JsonObject continuation = doc["faultResume"].to<JsonObject>();
  continuation["available"] = faultResume.available && phase == Phase::Fault;
  continuation["ready"] = !faultResumeProblem()[0];
  continuation["reason"] = faultResumeProblem();
  continuation["operation"] = faultResume.manual ? "manual" : "program";
  continuation["stepIndex"] = faultResume.step;
  if (faultResume.available) continuation["targetC"] = faultResume.reference; else continuation["targetC"] = nullptr;
  doc["sensorChecking"] = output.sensorChecking;
  doc["ready"] = !output.diagnostic && !output.updating && !output.sensorChecking && (apReady || stationReady) && sensorsHealthy() && phase != Phase::Fault && phase != Phase::Interrupted &&
                 !output.emergency && !output.recoveryBlocked && !recoveryJournal.data.pending &&
                 heaterFault() == heater::Fault::None && uint32_t(millis() - sampleAtMs) < heater::Guard::kStaleMs;
  doc["actuator"] = controlSettings.ssr ? "ssr" : "relay";
  doc["windowSec"] = controlSettings.windowMs / 1000; doc["maxPower"] = controlSettings.maxPower;
  auto protections = doc["protections"].to<JsonObject>();
  protections["probeDeltaEnabled"] = protectionSettings.probeDeltaEnabled;
  protections["maxProbeDeltaC"] = protectionSettings.maxProbeDeltaC;
  protections["meanOvershootEnabled"] = protectionSettings.meanOvershootEnabled;
  protections["maxMeanOvershootC"] = protectionSettings.maxMeanOvershootC;
  doc["cycleElapsedSec"] = !manualOperation && cycleActive() ? uint32_t(millis() - cycleStarted) / 1000 :
    activeRecipe.count && reportAvailable && lastReport.startMs == cycleStarted && lastReport.boot == bootCount ?
      uint32_t(lastReport.endMs - lastReport.startMs) / 1000 : 0;
  doc["stepRemainingSec"] = nullptr;
  doc["stepRemainingEstimated"] = false;
  if (manualOperation && manualSession.active()) doc["cycleStartedAtMs"] = manualSession.startedAt;
  else if (activeRecipe.count) doc["cycleStartedAtMs"] = cycleStarted; else doc["cycleStartedAtMs"] = nullptr;
  doc["stepElapsedSec"] = manualOperation || !cycleActive() ? 0 :
    (stepElapsedBefore + uint32_t((phase == Phase::Paused ? pausedAt : millis()) - stepStarted)) / 1000;
  doc["freeHeap"] = ESP.getFreeHeap(); doc["minFreeHeap"] = ESP.getMinFreeHeap();
  telegramBot.status(doc["telegram"].to<JsonObject>());
  doc["rssi"] = stationReady ? WiFi.RSSI() : 0;
  doc["sampleAgeMs"] = uint32_t(millis() - sampleAtMs);
  doc["duty"] = duty;
  JsonObject pid = doc["pid"].to<JsonObject>();
  pid["kp"] = kp; pid["ki"] = ki; pid["kd"] = kd;
  JsonObject pidState = doc["pidState"].to<JsonObject>();
  pidState["integralPct"] = pidController.integral;
  pidState["proportionalPct"] = pidController.proportional;
  pidState["derivativePct"] = kd * pidController.derivative;
  pidState["meanLimited"] = pidController.meanLimited;
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
    const bool toHold = profile::rampToHold(step, followingStep());
    doc["stepTransitionToHold"] = toHold;
    doc["stepCompletionBandC"] = step.type == profile::Type::Hold || toHold ? profile::kHoldBand : profile::kBand;
    doc["stepCompletionTarget"] = toHold ? followingStep()->target : step.target;
    if (cycleActive()) {
      const uint32_t elapsed = phase == Phase::Paused ? 0 : uint32_t(millis() - stepStarted);
      const float origin = phase == Phase::Paused ? pauseHold.targetC : stepStartC;
      const float remaining = profile::remainingSec(step, origin, elapsed, holdInBandMs,
        sensors[0].valid && sensors[1].valid ? fminf(sensors[0].c, sensors[1].c) : NAN,
        sensors[0].valid && sensors[1].valid ? hottestC() : NAN, followingStep());
      if (std::isfinite(remaining)) doc["stepRemainingSec"] = remaining;
      doc["stepRemainingEstimated"] = step.type != profile::Type::Hold;
      doc["stepWaitingForTemperature"] = step.type == profile::Type::Ramp &&
        profile::setpoint(step, origin, elapsed) >= step.target - 0.001f &&
        !profile::rampDone(step, origin, elapsed,
          sensors[0].valid && sensors[1].valid ? fminf(sensors[0].c, sensors[1].c) : NAN,
          sensors[0].valid && sensors[1].valid ? hottestC() : NAN, followingStep());
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
  interruption["pending"] = recoveryJournal.data.pending || outputSnapshot().recoveryBlocked;
  interruption["pendingAtBoot"] = interruptionAlreadyPending;
  interruption["powerRelated"] = lastInterruption.resetReason == ESP_RST_POWERON || lastInterruption.resetReason == ESP_RST_BROWNOUT;
  interruption["programName"] = interruptedProgram;
  interruption["estimateKnown"] = recovery::estimate(lastInterruption.checkpointUtc, lastInterruption.rebootUtc).known;
  interruption["checkpointUtc"] = lastInterruption.checkpointUtc;
  interruption["rebootUtc"] = lastInterruption.rebootUtc;
  interruption["upperBoundSec"] = lastInterruption.checkpointUtc && lastInterruption.rebootUtc >= lastInterruption.checkpointUtc ?
    lastInterruption.rebootUtc - lastInterruption.checkpointUtc : 0;
  doc["ntpSynced"] = ntpSynced;
  doc["utcSec"] = utcNow();
  doc["workshopConfigured"] = WORKSHOP_WIFI_SSID[0] != '\0';
  doc["workshopConnected"] = stationReady;
  doc["stationIp"] = stationReady ? WiFi.localIP().toString() : "";
  doc["stationMac"] = WORKSHOP_WIFI_SSID[0] ? WiFi.macAddress() : "";
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["recoveryRunning"] = wifiRecoveryRunning.load();
  wifi["recoveryPaused"] = !wifiRecoveryAllowed.load();
  wifi["statusCode"] = int(WiFi.status()); wifi["retryCount"] = wifiRetryCount.load();
  wifi["retryInSec"] = stationReady ? 0 : (wifiRetryWaitMs.load() + 999) / 1000;
  wifi["lastDisconnectReason"] = wifiLastReason.load();
  wifi["lastDisconnectMessage"] = network::reasonText(wifiLastReason.load());
  doc["apIp"] = apReady ? WiFi.softAPIP().toString() : "";
  doc["localName"] = mdnsReady ? String(MDNS_NAME) + ".local" : "";
  doc["uptimeMs"] = millis(); doc["sampleAtMs"] = sampleAtMs;
  doc["uptimeTotalMs"] = uint64_t(esp_timer_get_time()) / 1000;
  doc["http"]["running"] = server.active();
  doc["http"]["clients"] = server.capacity();
  doc["http"]["served"] = server.requestCount();
  doc["http"]["rejected"] = server.rejectedCount();
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
  kp = p; ki = i; kd = d; faultResume.clear(); resetPid();
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
  controlSettings = proposed; faultResume.clear(); resetPid();
  logEvent("COMANDO", String("Attuatore ") + actuator + "; finestra " + String(seconds) + " s; potenza max " + String(maxPower, 1) + "%");
  statusApi();
}
void protectionsApi() {
  if (cycleActive() || diagnosticMode() || outputSnapshot().updating) {
    error(409, "Ferma Programmi/Manuale ed esci da TEST prima di modificare i blocchi"); return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) { error(400, "JSON non valido"); return; }
  if (!doc["probeDeltaEnabled"].is<bool>() || !doc["meanOvershootEnabled"].is<bool>() ||
      (!doc["maxProbeDeltaC"].is<float>() && !doc["maxProbeDeltaC"].is<int>()) ||
      (!doc["maxMeanOvershootC"].is<float>() && !doc["maxMeanOvershootC"].is<int>())) {
    error(400, "Servono due abilitazioni booleane e due soglie numeriche"); return;
  }
  thermal::ProtectionSettings proposed;
  proposed.probeDeltaEnabled = doc["probeDeltaEnabled"].as<bool>();
  proposed.meanOvershootEnabled = doc["meanOvershootEnabled"].as<bool>();
  proposed.maxProbeDeltaC = doc["maxProbeDeltaC"].as<float>();
  proposed.maxMeanOvershootC = doc["maxMeanOvershootC"].as<float>();
  if (!thermal::validSettings(proposed)) { error(400, "Soglie da 0,1 a 430 °C, anche quando il blocco e disabilitato"); return; }
  if (prefs.putBytes("protections", &proposed, sizeof(proposed)) != sizeof(proposed)) {
    error(507, "Salvataggio blocchi fallito: restano applicate le impostazioni precedenti"); return;
  }
  protectionSettings = proposed;
  logEvent("PARAMETERS", String("Blocco delta sonde ") + (proposed.probeDeltaEnabled ? "ON >" : "OFF; soglia ") +
    String(proposed.maxProbeDeltaC,1) + " °C; blocco media-target " + (proposed.meanOvershootEnabled ? "ON >" : "OFF; soglia ") +
    String(proposed.maxMeanOvershootC,1) + " °C");
  statusApi();
}
void commandApi() {
  drainHeaterNotices();
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) { error(400, "JSON non valido"); return; }
  String action = doc["action"] | "";
  if (server.expired() && !server.expiredSafetyAction(action.c_str())) {
    error(408, "Comando scaduto: aggiorna lo stato e riprova"); return;
  }
  if (action == "emergency") {
    faultResume.clear();
    const bool alreadyBlocked = outputSnapshot().emergency;
    // Stop physical commands before NVS writes, report generation or logging.
    portENTER_CRITICAL(&heaterMux); outputs.emergencyStop(); ownerToken = 0; writeOutputs(); portEXIT_CRITICAL(&heaterMux);
    duty = 0;
    if (testActiveSaved) { prefs.putBool("testActive", false); testActiveSaved = false; }
    const bool saved = prefs.putBool("emergency", true) == sizeof(bool);
    trip("EMERGENZA: tutte le uscite spente; riconoscimento richiesto");
    logEvent("EMERGENZA", saved ? "Blocco uscite registrato; riconoscimento manuale richiesto" :
      "Blocco uscite attivo in RAM; salvataggio NVS fallito");
    if (!alreadyBlocked) telegramNotice("EMERGENZA FORNO", "Comando di tutte le uscite spento; blocco fino al riconoscimento locale.", true);
    statusApi(); return;
  }
  if (action == "stop") {
    faultResume.clear();
    portENTER_CRITICAL(&heaterMux); outputs.stopAll(); ownerToken = 0; writeOutputs(); portEXIT_CRITICAL(&heaterMux);
    stopTests("STOP manuale: test fermato"); inhibitHeater();
    logEvent("STOP", "STOP manuale: tutte le uscite spente");
    if (phase == Phase::Running || phase == Phase::Paused) stopCycle(Phase::Idle,
      manualOperation ? "Controllo manuale fermato dall'operatore" : "Ciclo fermato manualmente");
    statusApi(); return;
  }
  if (outputSnapshot().updating) { error(503, "Aggiornamento OTA in corso"); return; }
  if (action == "resetContinue") {
    if (phase != Phase::Fault || !faultResume.available) { error(409, faultResumeProblem()); return; }
    // Fresh conversions authorize the action, never the browser's last status.
    readSensorPair(); sampleAtMs = millis();
    const char *problem = faultResumeProblem();
    if (problem[0]) { error(409, problem); return; }
    const uint32_t now = millis();
    auto restored = faultResume;
    manual::Session resumedManual;
    float resumedOrigin = stepStartC; uint32_t resumedAt = stepStarted, resumedBefore = stepElapsedBefore;
    profile::HoldTimer resumedHold;
    const bool restoredOk = restored.manual ? restored.restore(now, resumedManual) :
      restored.restore(activeRecipe.steps[restored.step], now, resumedOrigin, resumedAt, resumedBefore, resumedHold);
    if (!restoredOk) { error(409, "Punto di ripresa non valido: usa RESET & BLOCK"); return; }
    stopTests("RESET & CONTINUE: uscite spente durante la verifica", true); resetPid();
    // Arm durable interruption tracking BEFORE clearing the output latches.
    if (!recoveryJournal.begin(recoveryStore, restored.manual ? "Controllo manuale" : activeRecipe.name.c_str(), utcNow()) ||
        prefs.putULong64("checkpoint", utcNow()) != sizeof(uint64_t) || prefs.putBool("active", true) != sizeof(bool)) {
      portENTER_CRITICAL(&heaterMux); outputs.blockRecovery(); writeOutputs(); portEXIT_CRITICAL(&heaterMux);
      recoveryJournal.data.pending = true; faultResume.clear();
      error(507, "Ripresa non salvata nella memoria di sicurezza: uscite bloccate. Usa RESET & BLOCK."); return;
    }
    if (!restored.manual && !cycleReport.continueAfterFault(now)) {
      portENTER_CRITICAL(&heaterMux); outputs.blockRecovery(); writeOutputs(); portEXIT_CRITICAL(&heaterMux);
      recoveryJournal.finish(recoveryStore); faultResume.clear();
      error(409, "Report del controllo non ripristinato: uscite bloccate"); return;
    }
    manualOperation = restored.manual;
    if (restored.manual) { manualSession = resumedManual; setpointC = manualSession.reference; }
    else {
      stepIndex = restored.step; stepStartC = resumedOrigin; stepStarted = resumedAt; stepElapsedBefore = resumedBefore;
      holdTimer = resumedHold; holdInBandMs = resumedHold.elapsedMs;
      setpointC = profile::setpoint(activeRecipe.steps[stepIndex], stepStartC, uint32_t(now-stepStarted));
    }
    pauseHold.clear(); faultResume.clear(); faultText = ""; cycleStopped = false; phase = Phase::Running;
    checkpoints.immediate(now);
    portENTER_CRITICAL(&heaterMux); outputs.guard.clear(); outputs.stopAll(); writeOutputs(); portEXIT_CRITICAL(&heaterMux);
    logEvent("CICLO", "RESET & CONTINUE: ripresa esplicita del controllo interrotto; attesa esclusa dal tempo attivo");
    telegramNotice(restored.manual ? "MANUALE RIPRESO DOPO ALLARME" : "CICLO RIPRESO", "RESET & CONTINUE: stesso punto, target e rate; tempo in banda conservato.");
    readSensorPair(); sampleAndControl(false); recordHistory(true); statusApi(); return;
  }
  if (action == "reset") {
    if (phase == Phase::Running || phase == Phase::Paused) { error(409, "Ferma prima il ciclo"); return; }
    stopTests("RESET & BLOCK: TEST fermato e uscite spente", true);
    readSensorPair(); sampleAtMs = millis();
    if (!sensorsHealthy()) { error(409, "Sonde o temperature fuori limite"); return; }
    if (prefs.putBool("active", false) != sizeof(bool) || !recoveryJournal.acknowledge(recoveryStore)) {
      error(507, "Riconoscimento interruzione non salvato: uscite ancora bloccate"); return;
    }
    if (outputSnapshot().emergency && prefs.putBool("emergency", false) != sizeof(bool)) {
      error(507, "Riconoscimento emergenza non salvato: uscite ancora bloccate"); return;
    }
    portENTER_CRITICAL(&heaterMux); outputs.acknowledgeEmergency(); outputs.acknowledgeRecovery(); outputs.guard.clear(); outputs.stopAll(); writeOutputs(); portEXIT_CRITICAL(&heaterMux);
    faultResume.clear(); phase = Phase::Idle; faultText = "";
    logEvent("INFO", "Allarme/interruzione riconosciuti dall'operatore"); statusApi(); return;
  }
  if (action != "stop" && diagnosticMode()) { error(409, "Esci dalla modalita TEST prima di comandare il ciclo"); return; }
  if (action == "pause") {
    if (manualOperation && cycleActive()) { error(409, "Nel manuale usa COOLDOWN o STOP"); return; }
    if (phase != Phase::Running) { error(409, "Nessun ciclo da mettere in pausa"); return; }
    readSensorPair(); sampleAtMs = millis();
    if (!sensorsHealthy() || heaterFault() != heater::Fault::None) {
      error(409, "Sonde/controllo fuori limite: pausa non attivata"); return;
    }
    const uint32_t now = millis();
    if (stepIndex >= activeRecipe.count || !pauseHold.begin(meanC(), now, stepStarted, stepElapsedBefore)) {
      error(409, "Temperatura o step non validi per il mantenimento in pausa"); return;
    }
    phase = Phase::Paused; pausedAt = now; cycleReport.setPaused(pausedAt, true); inhibitHeater(); duty = 0;
    holdTimer.update(pausedAt, false, false);
    sampleAndControl();
    if (phase == Phase::Paused) {
      logEvent("CICLO", "Ciclo in pausa: PID mantiene " + String(pauseHold.targetC, 1) + " °C; step sospeso");
      telegramNotice("CICLO IN PAUSA", "PID mantiene " + String(pauseHold.targetC, 1) + " °C; tempo dello step sospeso.");
      recordHistory(true);
    }
    statusApi(); return;
  }
  if (action == "resume") {
    if (phase != Phase::Paused) { error(409, "Nessun ciclo in pausa"); return; }
    readSensorPair(); sampleAtMs = millis();
    if (!sensorsHealthy() || heaterFault() != heater::Fault::None) { error(409, "Sonde/controllo fuori limite: riconosci l'allarme"); return; }
    if (stepIndex >= activeRecipe.count) { error(409, "Step non disponibile"); return; }
    const auto &requested = activeRecipe.steps[stepIndex];
    const float reference = requested.type == profile::Type::Hold ? requested.target : requested.type == profile::Type::Ramp ?
      fminf(requested.target,pauseHold.targetC) : fmaxf(requested.target,pauseHold.targetC);
    const String problem = protectionProblem(reference,requested.type == profile::Type::Cooldown);
    if (problem.length()) { error(409,problem); return; }
    const uint32_t now = millis();
    if (stepIndex >= activeRecipe.count || !pauseHold.resume(activeRecipe.steps[stepIndex], now,
        stepStartC, stepStarted, stepElapsedBefore)) { error(409, "Stato di pausa non valido"); return; }
    cycleReport.setPaused(now, false);
    phase = Phase::Running;
    setpointC = profile::setpoint(activeRecipe.steps[stepIndex], stepStartC, 0);
    logEvent("CICLO", "Ciclo ripreso dalla temperatura di pausa; stesso step e rate del programma");
    telegramNotice("CICLO RIPRESO", "Ripresa dello stesso step dalla temperatura di pausa."); sampleAndControl(); statusApi(); return;
  }
  if (action == "start") {
    if (phase == Phase::Running || phase == Phase::Paused) { error(409, "Ciclo gia in corso"); return; }
    if (phase == Phase::Fault || phase == Phase::Interrupted) { error(409, "Riconosci prima l'allarme o l'interruzione"); return; }
    if (recoveryJournal.data.pending || outputSnapshot().recoveryBlocked) { error(409, "Riconosci prima il blocco da interruzione"); return; }
    if (heaterFault() != heater::Fault::None) { error(409, "Supervisore in blocco: riconosci l'allarme"); return; }
    if (!doc["hardwareReady"].is<bool>() || !doc["hardwareReady"].as<bool>()) {
      error(400, "Conferma collaudo hardware e protezioni indipendenti prima di avviare"); return;
    }
    // Read again here: a stale status must never authorize a heat command.
    readSensorPair();
    sampleAtMs = millis();
    if (!sensorsHealthy()) { error(409, "Entrambe le PT100 devono essere valide e sotto il limite termico assoluto"); return; }
    const char *id = doc["recipeId"] | "";
    int selected = -1;
    for (size_t i = 0; i < recipeCount; ++i) if (recipes[i].id == id) { selected = int(i); break; }
    if (selected < 0) { error(404, "Programma non trovato"); return; }
    manualOperation = false; manualSession = manual::Session{};
    faultResume.clear();
    activeRecipe = recipes[selected];
    const String protection = protectionProblem(activeRecipe.steps[0].type == profile::Type::Hold ? activeRecipe.steps[0].target : meanC());
    if (protection.length()) { error(409,protection); return; }
    if (activeRecipe.steps[0].type == profile::Type::Ramp &&
        meanC() >= activeRecipe.steps[0].target - profile::kBand) {
      error(409, "Il forno e gia vicino/sopra il primo target: scegli un programma adatto"); return;
    }
    if (activeRecipe.steps[0].type == profile::Type::Cooldown &&
        meanC() <= activeRecipe.steps[0].target + profile::kBand) {
      error(409, "Il forno e gia sotto il target di cooldown"); return;
    }
    if (!recoveryJournal.begin(recoveryStore, activeRecipe.name.c_str(), utcNow()) ||
        prefs.putULong64("checkpoint", utcNow()) != sizeof(uint64_t) ||
        prefs.putBool("active", true) != sizeof(bool)) {
      portENTER_CRITICAL(&heaterMux); outputs.blockRecovery(); writeOutputs(); portEXIT_CRITICAL(&heaterMux);
      recoveryJournal.data.pending = true;
      phase = Phase::Interrupted;
      error(507, "Memoria di sicurezza non disponibile: ciclo non avviato"); return;
    }
    phase = Phase::Running; cycleStopped = false; stepIndex = 0; cycleStarted = millis();
    cycleReport.begin(cycleStarted, activeRecipe.steps, activeRecipe.count);
    auto &report = cycleReport.data;
    report.boot = bootCount; report.startUtc = utcNow();
    snprintf(report.recipeId, sizeof(report.recipeId), "%s", activeRecipe.id.c_str());
    snprintf(report.recipeName, sizeof(report.recipeName), "%s", activeRecipe.name.c_str());
    report.kp = kp; report.ki = ki; report.kd = kd; report.ssr = controlSettings.ssr;
    report.windowMs = controlSettings.windowMs; report.maxPower = controlSettings.maxPower;
    checkpoints.immediate(millis());
    logEvent("CICLO", "Ciclo partito: " + activeRecipe.name + "; PT100 reali");
    beginStep(); recordHistory(true, HISTORY_START);
    telegramNotice("CICLO AVVIATO", "Programma: " + activeRecipe.name, false, telegram::EventScope::Always);
    sampleAndControl(); statusApi(); return;
  }
  error(400, "Azione sconosciuta");
}
void manualApi() {
  drainHeaterNotices();
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) { error(400, "JSON non valido"); return; }
  const String action = doc["action"] | "";
  if (server.expired() && !server.expiredSafetyAction(action.c_str())) {
    error(408, "Comando scaduto: aggiorna lo stato e riprova"); return;
  }
  if (action == "stop") {
    faultResume.clear();
    if (manualOperation && cycleActive()) stopCycle(Phase::Idle, "Controllo manuale fermato dall'operatore");
    statusApi(); return;
  }
  const auto output = outputSnapshot();
  if (output.updating || output.diagnostic || output.emergency || output.recoveryBlocked || recoveryJournal.data.pending ||
      phase == Phase::Fault || phase == Phase::Interrupted || heaterFault() != heater::Fault::None) {
    error(409, "Controllo manuale bloccato: esci da TEST o riconosci prima allarmi/interruzioni"); return;
  }
  if (action == "cooldown") {
    if (!manualOperation || !cycleActive()) { error(409, "Nessun controllo manuale attivo"); return; }
    const float target = doc["targetC"].is<float>() ? doc["targetC"].as<float>() : NAN;
    const float rate = doc["rateCMin"].is<float>() ? doc["rateCMin"].as<float>() : NAN;
    if (!manualSession.cooldown(target, rate, millis())) {
      error(400, "Cooldown: target da 0 a 400 °C non superiore al setpoint e rate minimo 0,1 °C/min"); return;
    }
    inhibitHeater(); duty = 0; setpointC = manualSession.reference;
    sampleAndControl();
    if (phase == Phase::Running) {
      logEvent("MANUALE", "Cooldown manuale: verso " + String(target, 1) + " °C · " + String(rate, 2) + " °C/min; regolazione PID"); recordHistory(true);
      telegramNotice("MANUALE · COOLDOWN", "Verso " + String(target, 1) + " °C · " + String(rate, 2) + " °C/min; PID segue la discesa.");
    }
    statusApi(); return;
  }
  if (action != "start" && action != "target") { error(400, "Azione manuale sconosciuta"); return; }
  if ((!doc["targetC"].is<float>() && !doc["targetC"].is<int>()) || !thermal::validTarget(doc["targetC"].as<float>())) {
    error(400, "Target manuale numerico da 0 a 400 °C"); return;
  }
  if (!doc["rateCMin"].is<float>() || !manual::Session::validRate(doc["rateCMin"].as<float>())) {
    error(400, "Rate manuale numerico: minimo 0,1 °C/min"); return;
  }
  if (action == "start" && cycleActive()) { error(409, "Ferma il programma o il controllo manuale già attivo"); return; }
  if (action == "target" && (!manualOperation || !cycleActive())) { error(409, "Avvia prima il controllo manuale"); return; }
  if (action == "start" && (!doc["hardwareReady"].is<bool>() || !doc["hardwareReady"].as<bool>())) {
    error(400, "Conferma CHECK HARDWARE/AMBIENTE EFFETTUATO"); return;
  }
  readSensorPair(); sampleAtMs = millis();
  if (!sensorsHealthy()) { error(409, "Entrambe le PT100 devono essere valide e sotto il limite termico assoluto"); return; }
  const float requested = doc["targetC"].as<float>();
  const float rate = doc["rateCMin"].as<float>();
  const uint32_t now = millis();
  if (action == "start") {
    const String problem = protectionProblem(meanC());
    if (problem.length()) { error(409,problem); return; }
    manual::Session proposed;
    if (!proposed.start(requested, rate, hottestC(), meanC(), now)) { error(409, "Avvio manuale non valido"); return; }
    resetPid();
    if (!recoveryJournal.begin(recoveryStore, "Controllo manuale", utcNow()) ||
        prefs.putULong64("checkpoint", utcNow()) != sizeof(uint64_t) || prefs.putBool("active", true) != sizeof(bool)) {
      portENTER_CRITICAL(&heaterMux); outputs.blockRecovery(); writeOutputs(); portEXIT_CRITICAL(&heaterMux);
      recoveryJournal.data.pending = true; phase = Phase::Interrupted;
      error(507, "Memoria di sicurezza non disponibile: manuale non avviato"); return;
    }
    manualSession = proposed;
    faultResume.clear();
    manualOperation = true; phase = Phase::Running; cycleStopped = false;
    activeRecipe = Recipe{}; stepIndex = 0; cycleStarted = now;
    checkpoints.immediate(now);
    setpointC = manualSession.reference;
    logEvent("MANUALE", "Manuale avviato: target " + String(requested, 1) + " °C · rate " + String(rate, 2) + " °C/min");
    recordHistory(true, HISTORY_START);
    telegramNotice("MANUALE AVVIATO", "Target: " + String(requested, 1) + " °C · rate " + String(rate, 2) + " °C/min", false, telegram::EventScope::Always);
  } else {
    const bool wasCooldown = manualSession.mode == manual::Mode::Cooldown;
    const bool wasCooling = manualSession.coolingToTarget;
    if (!manualSession.changeTarget(requested, rate, hottestC(), meanC(), now)) { error(409, "Cambio target manuale non valido"); return; }
    if (wasCooldown || wasCooling || manualSession.coolingToTarget) resetPid();
    setpointC = manualSession.reference;
    logEvent("MANUALE", "Target manuale modificato: " + String(requested, 1) + " °C · rate " + String(rate, 2) + " °C/min"); recordHistory(true);
    telegramNotice("MANUALE · TARGET MODIFICATO", "Target: " + String(requested, 1) + " °C · rate " + String(rate, 2) + " °C/min");
  }
  sampleAndControl(); applyWindow(); statusApi();
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
  JsonDocument doc; doc["bootCount"] = bootCount; doc["capacity"] = historyCapacity;
  doc["intervalMs"] = HISTORY_MS;
  doc["oldestSeq"] = historyCount ? history[(historyHead + historyCapacity - historyCount) % historyCapacity].seq : 0;
  doc["latestSeq"] = historySeq;
  JsonArray list = doc["samples"].to<JsonArray>();
  size_t first = 0, end = historyCount;
  if (server.hasArg("before")) {
    uint32_t before = strtoul(server.arg("before").c_str(), nullptr, 10);
    while (end && history[(historyHead + historyCapacity - historyCount + end - 1) % historyCapacity].seq >= before) --end;
    first = end > HISTORY_PAGE ? end - HISTORY_PAGE : 0;
  } else if (server.hasArg("tail")) first = end > HISTORY_PAGE ? end - HISTORY_PAGE : 0;
  else {
    uint32_t after = server.hasArg("after") ? strtoul(server.arg("after").c_str(), nullptr, 10) : 0;
    while (first < end && history[(historyHead + historyCapacity - historyCount + first) % historyCapacity].seq <= after) ++first;
    if (end - first > HISTORY_PAGE) end = first + HISTORY_PAGE;
  }
  for (size_t i = first; i < end; ++i) {
    const Sample &s = history[(historyHead + historyCapacity - historyCount + i) % historyCapacity];
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
  if (server.expired() && !server.expiredSafetyAction(action.c_str())) {
    error(408, "Comando scaduto: aggiorna lo stato e riprova"); return;
  }
  // Test-only STOP cannot affect an autonomous recipe started by another page.
  if (action == "stop" || action == "exit") {
    if (!diagnosticMode()) { statusApi(); return; }
    stopTests(action == "exit" ? "Modalita TEST terminata: uscita spenta" : "STOP test: uscita spenta", action == "exit");
    statusApi(); return;
  }
  if (cycleActive()) { error(409, "Ferma il ciclo prima di usare TEST (anche in pausa)"); return; }
  if (phase == Phase::Fault || phase == Phase::Interrupted) { error(409, "Premi RESET & BLOCK prima di avviare TEST"); return; }
  if (outputSnapshot().emergency) { error(409, "Emergenza attiva: riconosci il blocco prima di usare TEST"); return; }
  if (recoveryJournal.data.pending || outputSnapshot().recoveryBlocked) {
    error(409, "Interruzione registrata: riconosci il blocco prima di usare TEST"); return;
  }
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
bool saveTelegramUsers(const telegram::Users &proposed) {
  if(!prefsReady||!telegram::commitUsers(telegramUsers,proposed,[](const telegram::Users &value){
    return prefs.putBytes("tgUsers",&value,sizeof(value))==sizeof(value);
  }))return false;
  telegramBot.setUsers(telegramUsers);return true;
}
void telegramUsersApi() {
  const uint32_t now=millis();telegramPending.expire(now);
  JsonDocument doc;
  const auto username=telegramBot.botUsername();
  doc["configured"]=telegramBot.configured;doc["running"]=telegramBot.statusRunning();
  doc["networkReady"]=workshopReady()&&ntpSynced&&!outputSnapshot().updating;
  doc["botUsername"]=username;doc["botUrl"]=username.length()?"https://t.me/"+username:String("");
  doc["maxUsers"]=telegram::kMaxUsers;
  auto list=doc["users"].to<JsonArray>();
  for(size_t i=0;i<telegram::kMaxUsers;++i){const auto &u=telegramUsers.entries[i];if(u.chat<=0)continue;
    auto row=list.add<JsonObject>();row["chatId"]=String(static_cast<long long>(u.chat));
    row["name"]=u.name;row["username"]=u.username;row["owner"]=i==0;row["mode"]=telegram::modeKey(telegram::modeFrom(u.mode));
  }
  auto pending=doc["pending"].to<JsonArray>();
  for(const auto &j:telegramPending.entries)if(j.user.chat>0){auto row=pending.add<JsonObject>();
    row["chatId"]=String(static_cast<long long>(j.user.chat));row["name"]=j.user.name;row["username"]=j.user.username;
    row["expiresInSec"]=(telegram::kJoinLifetimeMs-uint32_t(now-j.created)+999)/1000;
  }
  sendJson(200,doc);
}
void telegramUsersCommandApi(){
  if(outputSnapshot().updating){error(409,"Aggiornamento firmware in corso");return;}
  if(!telegramBot.configured||!telegramBot.statusRunning()){error(409,"Telegram non configurato o task non disponibile");return;}
  if(server.arg("plain").length()>1024){error(413,"Richiesta troppo grande");return;}
  JsonDocument input;if(deserializeJson(input,server.arg("plain"))){error(400,"JSON non valido");return;}
  const char *id=input["chatId"]|"";char *end=nullptr;
  const int64_t chat=strtoll(id,&end,10);
  if(!id[0]||!end||*end||chat<=0||strlen(id)>18){error(400,"ID Telegram non valido");return;}
  const String action=input["action"]|"";
  telegramPending.expire(millis());
  if(action=="reject"){if(!telegramPending.remove(chat)){error(404,"Richiesta scaduta o non disponibile");return;}telegramUsersApi();return;}
  auto proposed=telegramUsers;
  const int selected=telegram::modeIndex(input["mode"]|"");
  if(action=="approve"){
    if(!input["mode"].isNull()&&selected<0){error(400,"Modalità non valida");return;}
    const int i=telegramPending.find(chat);
    if(i<0){error(409,"Richiesta scaduta: l'utente deve inviare di nuovo /start");return;}
    auto user=telegramPending.entries[i].user;user.mode=uint8_t(selected<0?int(telegram::Mode::Brief):selected);
    if(!proposed.add(user)){error(409,"Utente già autorizzato o limite di 8 utenti raggiunto");return;}
  }else if(action=="remove"){
    if(!proposed.remove(chat)){error(409,"Il proprietario non è rimovibile; utente non disponibile");return;}
  }else if(action=="mode"){
    if(!proposed.setMode(chat,selected)){error(400,"Utente o modalità non validi");return;}
  }else{error(400,"Azione Telegram sconosciuta");return;}
  if(!saveTelegramUsers(proposed)){error(507,"Salvataggio utenti fallito: autorizzazioni precedenti mantenute");return;}
  if(action=="approve"){
    telegramPending.remove(chat);
    telegramBot.reply(String("✅ Accesso autorizzato a OvenControl.\n/status e /plot sono disponibili.\nModalità notifiche: ")+telegram::modeName(telegramBot.mode(chat))+".\nUsa /notifiche per cambiarla o /start per la guida.",true,false,chat);
  }
  logEvent("TELEGRAM",String("Utenti aggiornati: ")+action+" · ID "+String(static_cast<long long>(chat)),false);
  telegramUsersApi();
}
void handleTelegramRequest(const telegram::UserRequest &request){
  if(uint32_t(millis()-request.created)>telegram::kReplyLifetimeMs)return;
  const int64_t chat=request.user.chat;
  if(request.command==telegram::Command::None){
    if(telegramUsers.find(chat)>=0)return;
    if(telegramPending.add(request.user,millis()))
      telegramBot.reply("👋 Richiesta di accesso inviata. Il proprietario deve autorizzarti dalla scheda Telegram della dashboard. Poi potrai usare /status e /plot.",false,false,chat,true);
    return;
  }
  if(telegramUsers.find(chat)<0)return;
  if(request.command==telegram::Command::Status){telegramBot.reply(telegramStatus(chat),false,false,chat);return;}
  const int selected=request.command==telegram::Command::Essential?0:request.command==telegram::Command::Brief?1:request.command==telegram::Command::Detailed?2:-1;
  if(selected>=0){
    auto proposed=telegramUsers;proposed.setMode(chat,selected);
    if(telegramBot.mode(chat)==telegram::modeFrom(selected)||saveTelegramUsers(proposed))
      telegramBot.reply(String("✅ Modalità personale: ")+telegram::modeName(telegramBot.mode(chat))+".\n/status e /plot sempre disponibili.",true,true,chat);
    else telegramBot.reply("⚠️ Modalità non salvata: resta attiva quella precedente.",true,true,chat);
    return;
  }
  const unsigned minutes=request.command==telegram::Command::Plot15?15:request.command==telegram::Command::Plot30?30:request.command==telegram::Command::Plot60?60:0;
  if(minutes){
    const uint8_t flags=(sensors[0].valid?1:0)|(sensors[1].valid?2:0)|(std::isfinite(setpointC)?8:0);
    const Sample current{historySeq,millis(),sensors[0].c,sensors[1].c,setpointC,duty,flags,uint8_t(manualOperation?255:stepIndex)};
    if(!telegramBot.plot(history,historyCapacity,historyHead,historyCount,current,minutes,chat))
      telegramBot.reply("📈 Un grafico è già in preparazione o la memoria non è disponibile. Riprova tra poco.",false,false,chat);
  }
}
void setupOta() {
  if (!OVEN_OTA_PASSWORD[0]) { logEvent("OTA", "OTA disabilitato: configura include/ota_config.h"); return; }
  ArduinoOTA.setHostname(MDNS_NAME); ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.setPassword(OVEN_OTA_PASSWORD); ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.onStart([] {
    wifiRecoveryAllowed = false;
    telegramBot.setAvailable(false);
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
  bool connected = workshopReady();
  const auto reasonSeq = wifiReasonSeq.load();
  if (reasonSeq != wifiLoggedReasonSeq) {
    wifiLoggedReasonSeq = reasonSeq;
    const auto reason = wifiLastReason.load();
    logEvent("ATTENZIONE", "Wi-Fi capannone: " + String(network::reasonText(reason)) + " · codice " + String(reason), false);
  }
  const auto retries = wifiRetryCount.load();
  if (retries != wifiLoggedRetryCount) {
    wifiLoggedRetryCount = retries;
    logEvent("RETE", "Recupero Wi-Fi, tentativo " + String(retries) +
      (wifiLastRetryAccepted.load() ? ": richiesta inviata" : ": nessuna conferma immediata, verificare stato/errore"), false);
  }
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
  portENTER_CRITICAL(&heaterMux); outputServerReady = server.active() && (apReady || connected); portEXIT_CRITICAL(&heaterMux);
  if (connected && (!ntpStarted || (!ntpSynced && uint32_t(millis() - lastNtpAttempt) >= 60000))) {
    lastNtpAttempt = millis();
    configTime(0, 0, WORKSHOP_NTP_SERVER); ntpStarted = true;
    logEvent("RETE", "Sincronizzazione ora NTP in corso");
  }
  if (ntpStarted && !ntpSynced && time(nullptr) >= 1700000000 &&
      sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
    ntpSynced = true; logEvent("RETE", "Ora NTP sincronizzata");
    if (hadInterrupted && previousCheckpoint && !lastInterruption.rebootUtc) {
      uint64_t bootEpoch = uint64_t(time(nullptr)) - millis() / 1000;
      if (bootEpoch >= previousCheckpoint) {
        outageUpperBoundSec = bootEpoch - previousCheckpoint;
        lastInterruption.rebootUtc = bootEpoch;
        recoveryJournal.returned(recoveryStore, bootEpoch);
        prefs.putBytes("interruption", &lastInterruption, sizeof(lastInterruption));
        logEvent("ATTENZIONE", "Ciclo interrotto; tempo massimo senza controllo " +
                 String((unsigned long)outageUpperBoundSec) + " s (non durata esatta del blackout)");
      }
    }
  }
  if (ntpSynced && workshopReady() && bootNoticePending) {
    bootNoticePending = false;
    recovery::BootState state;
    state.cause = bootCause;
    state.interrupted = hadInterrupted;
    state.interruptionCause = lastInterruption.resetReason == ESP_RST_POWERON ? recovery::Cause::PowerOn :
      lastInterruption.resetReason == ESP_RST_BROWNOUT ? recovery::Cause::Brownout : recovery::Cause::Other;
    state.alreadyPending = interruptionAlreadyPending;
    state.testInterrupted = testInterrupted;
    state.emergency = outputSnapshot().emergency;
    // NTP may arrive after the operator has acknowledged an old latch. Do not
    // announce that old event as still pending once it has been cleared.
    if (interruptionAlreadyPending && !recoveryJournal.data.pending && !outputSnapshot().recoveryBlocked)
      state.interrupted = false;
    const auto notice = recovery::bootNotice(state);
    String reason = "Causa reset ESP: " + resetReason + ".\nAl riavvio: comando riscaldamento SPENTO. Ripresa automatica: DISABILITATA.";
    if (notice.power) reason += "\nRipristino/calo dell'alimentazione ESP rilevato; può essere un blackout o una riaccensione manuale.";
    if (hadInterrupted) {
      if (interruptionAlreadyPending)
        reason += "\nInterruzione precedente già presente all'avvio: questo riavvio non indica un nuovo blackout durante il controllo.";
      reason += "\nProgramma interrotto: " + (interruptedProgram.length() ? interruptedProgram : String("non disponibile (registro precedente)"));
      const auto estimate = recovery::estimate(lastInterruption.checkpointUtc, lastInterruption.rebootUtc);
      if (estimate.known) {
        reason += "\nIntervallo senza controllo fino al riavvio: al massimo circa " + telegramTime(uint32_t(estimate.upperSec)) + ".";
        char date[64]; reportpdf::date(lastInterruption.checkpointUtc, date, sizeof(date));
        reason += "\nUltimo checkpoint: " + String(date) + ".\nStima dall'ultimo salvataggio, non durata esatta del blackout.";
      } else reason += "\nDurata: SCONOSCIUTA (checkpoint/NTP assente o orologio incoerente).";
      reason += outputSnapshot().recoveryBlocked || recoveryJournal.data.pending ?
        "\nUscite bloccate fino al riconoscimento locale. Per ripartire serve un nuovo avvio manuale dall'inizio." :
        "\nInterruzione già riconosciuta localmente dall'operatore. Nessuna ripresa automatica è stata eseguita.";
    } else if (testInterrupted) reason += "\nTEST interrotto: uscite spente, nessuna ripresa automatica. Durata dell'interruzione non disponibile.";
    else reason += "\nNessun controllo attivo interrotto. L'accensione non richiede RESET ERRORI, salvo altri blocchi presenti.";
    if (outputSnapshot().emergency) reason += "\nEMERGENZA PERSISTENTE: riconoscimento locale richiesto.";
    telegramNotice(notice.title, reason + "\nAvvio firmware completato. Wi-Fi capannone connesso: " + WiFi.localIP().toString() + ".",
      notice.critical, telegram::EventScope::Always);
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
  history = static_cast<Sample *>(ps_calloc(HISTORY_CAP, sizeof(Sample)));
  historyCapacity = history ? HISTORY_CAP : HISTORY_PAGE;
  if (!history) history = static_cast<Sample *>(calloc(historyCapacity, sizeof(Sample)));
  if (!history) historyCapacity = 0;
  if (historyCapacity != HISTORY_CAP)
    logEvent("ATTENZIONE", "Storico dashboard ridotto: PSRAM non disponibile; campioni " + String(historyCapacity));
  reportHistory = static_cast<Sample *>(ps_malloc(sizeof(Sample) * (historyCapacity + 1)));
  if (!reportHistory) reportHistory = static_cast<Sample *>(calloc(historyCapacity + 1, sizeof(Sample)));
  if (!reportHistory) logEvent("ATTENZIONE", "Memoria curva report non disponibile; riepilogo conservato senza grafico");
  if (prefsReady && !logs::makeRoom(logStore, persistedSeq, PERSIST_CAP, logs::kReserveEntries))
    Serial.println("[ATTENZIONE] NVS quasi piena anche dopo la pulizia dei vecchi log full");
  testInterrupted = prefs.getBool("testActive", false); prefs.putBool("testActive", false);
  bool wasActive = prefs.getBool("active", false);
  bootCount = prefs.getUInt("boots", 0) + 1; prefs.putUInt("boots", bootCount);
  const auto bootReset = esp_reset_reason();
  resetReason = String(bootReset);
  bootCause = bootReset == ESP_RST_POWERON ? recovery::Cause::PowerOn :
    bootReset == ESP_RST_BROWNOUT ? recovery::Cause::Brownout : recovery::Cause::Other;
  const bool journalLoaded = recoveryJournal.load(recoveryStore);
  interruptionAlreadyPending = journalLoaded && recoveryJournal.data.pending;
  const bool blockSaved = recoveryJournal.boot(recoveryStore, wasActive,
    prefs.getULong64("checkpoint", 0), bootReset);
  hadInterrupted = recoveryJournal.data.pending;
  if (hadInterrupted) {
    // Persist the latch before clearing the old active flag; repeated resets
    // remain blocked until the operator explicitly acknowledges the event.
    portENTER_CRITICAL(&heaterMux); outputs.blockRecovery(); writeOutputs(); portEXIT_CRITICAL(&heaterMux);
    if (blockSaved) prefs.putBool("active", false);
    previousCheckpoint = recoveryJournal.data.checkpointUtc;
    interruptedProgram = recoveryJournal.data.program;
    lastInterruption.resetReason = recoveryJournal.data.resetReason;
    lastInterruption.checkpointUtc = previousCheckpoint;
    lastInterruption.rebootUtc = recoveryJournal.data.rebootUtc;
    interruptionRecorded = true;
    prefs.putBytes("interruption", &lastInterruption, sizeof(lastInterruption));
    prefs.putString("interName", interruptedProgram);
  } else if (prefs.getBytesLength("interruption") == sizeof(lastInterruption) &&
             prefs.getBytes("interruption", &lastInterruption, sizeof(lastInterruption)) == sizeof(lastInterruption)) {
    interruptionRecorded = true;
    interruptedProgram = prefs.getString("interName", "");
  }
  if (!journalLoaded || !blockSaved) logEvent("ALLARME", "Registro ripresa non leggibile/salvato: blocco uscite fino al riconoscimento", false);
  phase = hadInterrupted ? Phase::Interrupted : Phase::Idle;
  if (prefs.getBool("emergency", false)) {
    portENTER_CRITICAL(&heaterMux); outputs.emergencyStop(); writeOutputs(); portEXIT_CRITICAL(&heaterMux);
    phase = Phase::Fault; faultText = "EMERGENZA registrata: uscite bloccate fino al riconoscimento";
  }
  logEvent(hadInterrupted ? "ATTENZIONE" : "INFO", hadInterrupted ?
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
  if (prefs.isKey("protections")) {
    thermal::ProtectionSettings saved;
    if (prefs.getBytesLength("protections") == sizeof(saved) &&
        prefs.getBytes("protections", &saved, sizeof(saved)) == sizeof(saved) && thermal::validSettings(saved))
      protectionSettings = saved;
    else logEvent("ATTENZIONE", "Blocchi termici salvati non validi: attivati delta sonde >30 °C e media-target >10 °C");
  }
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
  readSensorPair(); sampleAndControl(false); recordHistory(true);
  // Credentials come from the firmware; writing them again into the shared
  // NVS can fail when persistent event logs have filled that partition.
  // This must precede mode(), which initializes the Wi-Fi driver.
  WiFi.persistent(false);
  WiFi.setHostname(MDNS_NAME);
  WiFi.onEvent(wifiEvent);
  bool wifiReady = WiFi.mode(WORKSHOP_WIFI_SSID[0] ? WIFI_AP_STA : WIFI_AP);
  if (!wifiReady) logEvent("ALLARME", "Driver Wi-Fi non avviato");
  apReady = wifiReady && WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (!apReady) logEvent("ALLARME", "Rete Wi-Fi diretta non avviata");
  if (wifiReady && WORKSHOP_WIFI_SSID[0]) {
    WiFi.setAutoReconnect(true); WiFi.begin(WORKSHOP_WIFI_SSID, WORKSHOP_WIFI_PASSWORD);
    logEvent("RETE", "Connessione Wi-Fi capannone in corso");
  }
  setupOta();
  server.on("/api/report", ovenhttp::Method::Get, reportApi);
  server.on("/api/report/history", ovenhttp::Method::Get, reportHistoryApi);
  server.on("/api/test/command", ovenhttp::Method::Post, testCommandApi, true);
  server.on("/api/test/heartbeat", ovenhttp::Method::Post, testHeartbeatApi);
  server.on("/api/test/history", ovenhttp::Method::Get, testHistoryApi);
  server.on("/", ovenhttp::Method::Get, [] { server.sendHeader("Cache-Control", "no-store"); server.sendHeader("Content-Encoding", "gzip"); server.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char *>(WEB_HTML), sizeof(WEB_HTML)); });
  const auto serveFavicon = [] {
    server.sendHeader("Cache-Control", "public, max-age=86400");
    server.send_P(200, "image/jpeg", reinterpret_cast<const char *>(WEB_FAVICON), sizeof(WEB_FAVICON));
  };
  server.on("/favicon.jpg", ovenhttp::Method::Get, serveFavicon);
  server.on("/favicon.ico", ovenhttp::Method::Get, serveFavicon);
  server.on("/style.css", ovenhttp::Method::Get, [] { server.sendHeader("Cache-Control", "no-store"); server.sendHeader("Content-Encoding", "gzip"); server.send_P(200, "text/css; charset=utf-8", reinterpret_cast<const char *>(WEB_CSS), sizeof(WEB_CSS)); });
  server.on("/app.js", ovenhttp::Method::Get, [] { server.sendHeader("Cache-Control", "no-store"); server.sendHeader("Content-Encoding", "gzip"); server.send_P(200, "application/javascript; charset=utf-8", reinterpret_cast<const char *>(WEB_JS), sizeof(WEB_JS)); });
  server.on("/report-export.js", ovenhttp::Method::Get, [] { server.sendHeader("Cache-Control", "no-store"); server.sendHeader("Content-Encoding", "gzip"); server.send_P(200, "application/javascript; charset=utf-8", reinterpret_cast<const char *>(WEB_EXPORT_JS), sizeof(WEB_EXPORT_JS)); });
  server.on("/jspdf.umd.min.js", ovenhttp::Method::Get, [] {
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "application/javascript", reinterpret_cast<const char *>(WEB_PDF_JS), sizeof(WEB_PDF_JS));
  });
  server.on("/api/status", ovenhttp::Method::Get, [] { statusApi(); });
  server.on("/api/telegram/users",ovenhttp::Method::Get,telegramUsersApi);
  server.on("/api/telegram/users",ovenhttp::Method::Post,telegramUsersCommandApi);
  server.on("/api/recipes", ovenhttp::Method::Get, recipesApi);
  server.on("/api/recipes", ovenhttp::Method::Post, saveRecipesApi);
  server.on("/api/command", ovenhttp::Method::Post, commandApi, true);
  server.on("/api/manual", ovenhttp::Method::Post, manualApi, true);
  server.on("/api/pid", ovenhttp::Method::Post, pidApi);
  server.on("/api/control", ovenhttp::Method::Post, controlSettingsApi);
  server.on("/api/protections", ovenhttp::Method::Post, protectionsApi);
  server.on("/api/logs", ovenhttp::Method::Get, logsApi);
  server.on("/api/history", ovenhttp::Method::Get, historyApi);
  server.onNotFound([] { error(404, "Percorso non trovato"); });
  if (!server.begin()) logEvent("ALLARME", "Server dashboard non avviato: memoria/task non disponibili", false);
  if (WORKSHOP_WIFI_SSID[0]) {
    wifiRecoveryRunning = xTaskCreatePinnedToCore(wifiRecoveryTask, "wifi-recovery", 4096, nullptr, 1, nullptr, 0) == pdPASS;
    if (!wifiRecoveryRunning) logEvent("ATTENZIONE", "Recupero Wi-Fi non avviato; riconnessione della libreria ancora attiva", false);
  }
  chipTemperatureC = temperatureRead();
  lastChipTemperatureAt = millis();
  if (apReady) logEvent("RETE", "Dashboard diretta: http://" + WiFi.softAPIP().toString() + "/");
  telegramUsers.begin(strtoll(OVEN_TELEGRAM_CHAT_ID,nullptr,10),telegram::modeFrom(prefs.getUChar("tgMode",uint8_t(telegram::Mode::Brief))));
  if(prefs.isKey("tgUsers")){
    telegram::Users saved;
    if(prefs.getBytesLength("tgUsers")==sizeof(saved)&&prefs.getBytes("tgUsers",&saved,sizeof(saved))==sizeof(saved)&&saved.valid(telegramUsers.entries[0].chat))telegramUsers=saved;
    else logEvent("ATTENZIONE","Elenco Telegram non valido: resta autorizzato solo il proprietario configurato",false);
  }
  telegramBot.setUsers(telegramUsers);
  if (telegramBot.begin(OVEN_TELEGRAM_TOKEN, OVEN_TELEGRAM_CHAT_ID)) {
    logEvent("TELEGRAM", "Task notifiche attivo; utenti autorizzati gestiti dalla scheda Telegram", false);
    // Compose one alert with its time estimate once NTP is available, before
    // enabling Telegram's verified TLS connection.
    bootNoticePending = true;
  } else if (OVEN_TELEGRAM_TOKEN[0] || OVEN_TELEGRAM_CHAT_ID[0])
    logEvent("TELEGRAM", "Notifiche non avviate: verifica configurazione privata, PSRAM e memoria disponibile", false);
}
void loop() {
  drainHeaterNotices();
  if (otaEnabled) ArduinoOTA.handle();
  wifiRecoveryAllowed = !outputSnapshot().updating;
  telegramBot.setAvailable(workshopReady() && ntpSynced && !outputSnapshot().updating);
  telegram::UserRequest request;
  if(telegramBot.takeRequest(request))handleTelegramRequest(request);
  if (outputSnapshot().updating) { delay(5); return; }
  server.handleClient();
  updateNetwork();
  uint32_t now = millis();
  if ((phase == Phase::Running || phase == Phase::Paused) && uint32_t(now - sampleAtMs) >= heater::Guard::kStaleMs)
    trip("Lettura sonde troppo vecchia: uscita spenta");
  if (uint32_t(now - lastSampleAt) >= SAMPLE_MS) {
    lastSampleAt = now;
    if (!sensorBatchActive) startSensorPair();
  }
  if (pollSensorPair()) { sampleAndControl(false); recordHistory(); recordTestHistory(); }
  applyWindow();
  if ((phase == Phase::Running || phase == Phase::Paused) && ntpSynced &&
      checkpoints.take(now)) {
    uint64_t current = utcNow();
    if (current) {
      // Rate-limit attempts too: a full/unavailable NVS must not be hammered
      // every loop while regulation and the safety supervisor are running.
      if (recoveryJournal.checkpoint(recoveryStore, current)) {
        checkpointWriteFaultLogged = false;
      } else if (!checkpointWriteFaultLogged) {
        checkpointWriteFaultLogged = true;
        logEvent("ATTENZIONE", "Checkpoint ora non salvato: stima interruzione meno precisa o sconosciuta");
        telegramNotice("ALLARME CHECKPOINT", "Checkpoint non salvato; stima blackout meno precisa o sconosciuta. Il controllo prosegue con il precedente registro.", true);
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
