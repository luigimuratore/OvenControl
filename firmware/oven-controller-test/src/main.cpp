#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_MAX31865.h>
#include <esp_system.h>
#include <math.h>
#include <time.h>
#include <esp_sntp.h>
#include "web_assets.h"
#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
#define WORKSHOP_WIFI_SSID ""
#define WORKSHOP_WIFI_PASSWORD ""
#define WORKSHOP_NTP_SERVER "pool.ntp.org"
#endif

// HW-678 / ESP32-S3-WROOM-1 N16R8. GPIO35/36 are reserved for octal PSRAM.
constexpr int PIN_CS1 = 14, PIN_CS2 = 10;
constexpr int PIN_SCK = 13, PIN_MISO = 12, PIN_MOSI = 11;
constexpr int PIN_RELAY = 4, PIN_RED = 6, PIN_GREEN = 7;
constexpr float RREF = 470.0f, RNOMINAL = 100.0f;
constexpr float TARGET_C = 35.0f, TEST_CUTOFF_C = 36.0f, TEST_REARM_C = 35.5f;
constexpr float MAX_MEASURED = 205.0f, MAX_DELTA = 10.0f;
constexpr float PID_KP = 35.0f, PID_KI = 0.10f, PID_KD = 12.0f;
constexpr uint32_t PID_WINDOW_MS = 5000;
constexpr uint32_t SAMPLE_MS = 500, MAX_TEST_MS = 4UL * 60UL * 60UL * 1000UL;
constexpr uint32_t HISTORY_SAMPLE_MS = 10000;
constexpr size_t HISTORY_CAP = 2160, HISTORY_PAGE = 180; // Six hours at one point per 10 s.
constexpr uint32_t CHECKPOINT_MS = 30000;
constexpr char AP_SSID[] = "OvenController-Test";
constexpr char AP_PASSWORD[] = "oven12345";
constexpr char MDNS_HOST[] = "oven-controller";

Adafruit_MAX31865 rtd1(PIN_CS1), rtd2(PIN_CS2);
WebServer server(80);
Preferences prefs;

struct Sensor { float c = NAN; uint8_t fault = 0; bool valid = false; };
Sensor sensors[2];
bool running = false, relayOn = false, faultLatched = false, interrupted = false, upperCutoff = false;
bool apReady = false, timeKnown = false, ntpStarted = false, ntpSynced = false;
bool workshopWasConnected = false, mdnsReady = false;
bool greenLedOn = false;
float chipTemperatureC = NAN;
uint32_t lastChipTemperatureAt = 0;
String workshopIp;
bool targetLogged = false, outageLogged = false, simulatedSensors = false, sensorFaultLogged = false;
float pidDuty = 0.0f, pidIntegral = 0.0f, pidDerivative = 0.0f, previousAverage = NAN;
float simTemperature = 25.0f, simStoredHeat = 0.0f;
String faultText, resetReason;
uint32_t lastSample = 0, sampleAtMs = 0, runStart = 0, bootCount = 0, pidSampleAt = 0, windowStart = 0;
uint32_t checkpointAt = 0, simSampleAt = 0, pidLogAt = 0;
uint32_t lastNetworkCheck = 0;
uint64_t wallAtSync = 0, syncAtMs = 0, previousCheckpoint = 0, outageUpperBoundSec = 0;
uint32_t eventSeq = 0, persistedSeq = 0;
struct Event { uint32_t seq; uint64_t utc; String level, message; bool saved; };
constexpr size_t EVENT_CAP = 100, PERSIST_CAP = 40;
Event events[EVENT_CAP];
size_t eventHead = 0, eventCount = 0;
struct HistorySample { uint32_t seq, atMs; float sensor1, sensor2, duty; uint8_t flags; };
HistorySample historySamples[HISTORY_CAP];
size_t historyHead = 0, historyCount = 0;
uint32_t historySeq = 0, lastHistorySampleAt = 0;

uint64_t nowUtc() {
  if (ntpSynced) return (uint64_t)time(nullptr);
  if (!timeKnown) return 0;
  return wallAtSync + (uint32_t)(millis() - syncAtMs) / 1000;
}

void persistEvent(const Event &e) {
  // Keep only significant events; a power cut must leave the last event available.
  char utcText[24];
  snprintf(utcText, sizeof(utcText), "%llu", (unsigned long long)e.utc);
  String line = String(utcText) + "|" + e.level + "|" + e.message;
  ++persistedSeq;
  prefs.putString(("log" + String(persistedSeq % PERSIST_CAP)).c_str(), line);
  prefs.putUInt("logseq", persistedSeq);
}

void logEvent(const String &level, const String &message, bool save = true) {
  Event e{++eventSeq, nowUtc(), level, message, save};
  events[eventHead] = e;
  eventHead = (eventHead + 1) % EVENT_CAP;
  if (eventCount < EVENT_CAP) ++eventCount;
  if (save) persistEvent(e);
  Serial.printf("[%s] %s\n", level.c_str(), message.c_str());
}

void loadEvents() {
  persistedSeq = prefs.getUInt("logseq", 0);
  uint32_t first = persistedSeq > PERSIST_CAP ? persistedSeq - PERSIST_CAP + 1 : 1;
  for (uint32_t n = first; n <= persistedSeq; ++n) {
    String line = prefs.getString(("log" + String(n % PERSIST_CAP)).c_str(), "");
    int a = line.indexOf('|'), b = line.indexOf('|', a + 1);
    if (a < 0 || b < 0) continue;
    Event e{++eventSeq, strtoull(line.substring(0, a).c_str(), nullptr, 10),
            line.substring(a + 1, b), line.substring(b + 1), true};
    events[eventHead] = e;
    eventHead = (eventHead + 1) % EVENT_CAP;
    if (eventCount < EVENT_CAP) ++eventCount;
  }
}

void setRelay(bool on) {
  if (relayOn == on) return;
  relayOn = on;
  digitalWrite(PIN_RELAY, on ? HIGH : LOW);
  digitalWrite(PIN_RED, on ? HIGH : LOW);
  if (running) logEvent("USCITA", on ? "GPIO4 ALTO: comando rele attivo, LED rosso acceso" :
                         "GPIO4 BASSO: comando rele spento, LED rosso spento", false);
}

void stopTest(const String &why) {
  setRelay(false);
  pidDuty = 0;
  if (running) {
    running = false;
    prefs.putBool("active", false);
    logEvent("INFO", why);
  }
}

void trip(const String &why) {
  setRelay(false);
  pidDuty = 0;
  running = false;
  prefs.putBool("active", false);
  if (!faultLatched || faultText != why) logEvent("ALLARME", why);
  faultLatched = true;
  faultText = why;
}

void applyRelayWindow() {
  if (!running || faultLatched || upperCutoff || !sensors[0].valid || !sensors[1].valid) {
    setRelay(false); return;
  }
  uint32_t elapsed = (uint32_t)(millis() - windowStart);
  if (elapsed >= PID_WINDOW_MS) {
    windowStart += (elapsed / PID_WINDOW_MS) * PID_WINDOW_MS;
    elapsed = (uint32_t)(millis() - windowStart);
  }
  setRelay(elapsed < (uint32_t)(pidDuty * PID_WINDOW_MS / 100.0f));
}

void readOne(Adafruit_MAX31865 &rtd, Sensor &s) {
  rtd.clearFault();
  float c = rtd.temperature(RNOMINAL, RREF);
  uint8_t f = rtd.readFault();
  s.c = c; s.fault = f;
  s.valid = (f == 0 && isfinite(c) && c >= -20.0f && c <= 220.0f);
}

void updateSimulatedSensors() {
  uint32_t now = millis();
  float dt = simSampleAt ? fminf((uint32_t)(now - simSampleAt) / 1000.0f, 2.0f) : 0.0f;
  simSampleAt = now;
  // A simple test plant: the relay warms a stored-heat term; without power it cools.
  if (dt > 0) {
    float requestedHeat = relayOn ? 1.0f : 0.0f;
    simStoredHeat += (requestedHeat - simStoredHeat) * fminf(dt / 8.0f, 1.0f);
    simTemperature += (0.52f * simStoredHeat - 0.005f * (simTemperature - 24.0f)) * dt;
    simTemperature = constrain(simTemperature, 24.0f, 45.0f);
  }
  sensors[0].c = simTemperature + 0.12f;
  sensors[1].c = simTemperature - 0.12f;
  for (auto &sensor : sensors) { sensor.fault = 0; sensor.valid = true; }
}

void recordHistory(bool force = false) {
  uint32_t now = millis();
  if (!force && (uint32_t)(now - lastHistorySampleAt) < HISTORY_SAMPLE_MS) return;
  lastHistorySampleAt = now;
  uint8_t flags = (sensors[0].valid ? 1 : 0) | (sensors[1].valid ? 2 : 0) |
                  (relayOn ? 4 : 0) | (simulatedSensors ? 8 : 0);
  historySamples[historyHead] = {++historySeq, now,
      sensors[0].valid ? sensors[0].c : 0.0f,
      sensors[1].valid ? sensors[1].c : 0.0f, pidDuty, flags};
  historyHead = (historyHead + 1) % HISTORY_CAP;
  if (historyCount < HISTORY_CAP) ++historyCount;
}

void sampleAndControl() {
  sampleAtMs = millis();
  if (simulatedSensors) updateSimulatedSensors();
  else { readOne(rtd1, sensors[0]); readOne(rtd2, sensors[1]); }
  if (!sensors[0].valid || !sensors[1].valid) {
    if (!simulatedSensors && !sensorFaultLogged) {
      sensorFaultLogged = true;
      logEvent("ATTENZIONE", "Una o entrambe le PT100 reali non sono disponibili");
    }
    if (running) trip("Sonda PT100 non valida o MAX31865 in fault: uscita spenta");
    else setRelay(false);
    return;
  }
  if (!simulatedSensors && sensorFaultLogged) {
    sensorFaultLogged = false;
    logEvent("INFO", "Entrambe le PT100 reali leggibili");
  }
  float a = sensors[0].c, b = sensors[1].c;
  if (a >= MAX_MEASURED || b >= MAX_MEASURED) { trip("Sovratemperatura misurata: uscita spenta"); return; }
  if (fabsf(a - b) > MAX_DELTA) { trip("Differenza tra sonde oltre 10 °C: uscita spenta"); return; }
  if (!running || faultLatched) { setRelay(false); return; }
  if ((uint32_t)(millis() - runStart) > MAX_TEST_MS) { stopTest("Test fermato dopo 4 ore"); return; }
  float avg = (a + b) / 2.0f;
  if (!targetLogged && avg >= TARGET_C) {
    targetLogged = true;
    logEvent("INFO", "Target 35 °C raggiunto; regolazione PID ancora attiva");
  }
  float hottest = fmaxf(a, b);
  if (hottest >= TEST_CUTOFF_C && !upperCutoff) {
    upperCutoff = true;
    pidDuty = 0;
    pidIntegral = 0;
    pidDerivative = 0;
    if (simulatedSensors) simStoredHeat = 0;
    setRelay(false);
    logEvent("INFO", "Una sonda ha raggiunto 36 °C: rele spento");
  }
  if (upperCutoff) {
    if (hottest > TEST_REARM_C) { setRelay(false); return; }
    upperCutoff = false;
    previousAverage = avg;
    pidDerivative = 0;
    pidSampleAt = millis();
    windowStart = millis();
    logEvent("INFO", "Sonde a 35,5 °C o meno: controllo PID riattivato");
  }

  // PID on the mean temperature. Output is a duty cycle in a slow 5 s window.
  uint32_t now = millis();
  float dt = pidSampleAt ? (uint32_t)(now - pidSampleAt) / 1000.0f : 1.0f;
  dt = constrain(dt, 0.5f, 2.0f);
  float error = TARGET_C - avg;
  float rawDerivative = isfinite(previousAverage) ? -(avg - previousAverage) / dt : 0.0f;
  pidDerivative = 0.75f * pidDerivative + 0.25f * rawDerivative;
  float proposedIntegral = constrain(pidIntegral + PID_KI * error * dt, 0.0f, 100.0f);
  float raw = PID_KP * error + proposedIntegral + PID_KD * pidDerivative;
  // Do not wind up the integrator while the output is saturated in the same direction.
  if (!((raw > 100.0f && error > 0) || (raw < 0.0f && error < 0))) pidIntegral = proposedIntegral;
  pidDuty = constrain(PID_KP * error + pidIntegral + PID_KD * pidDerivative, 0.0f, 100.0f);
  pidSampleAt = now;
  previousAverage = avg;
  applyRelayWindow();
  if ((uint32_t)(now - pidLogAt) >= 15000) {
    pidLogAt = now;
    logEvent("PID", "Media " + String(avg, 1) + " °C; target 35 °C; comando " +
             String(pidDuty, 0) + "%", false);
  }
}

void sendJson(int status, const JsonDocument &doc) {
  String body;
  serializeJson(doc, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(status, "application/json; charset=utf-8", body);
}

void sendError(int status, const char *message) {
  JsonDocument doc; doc["error"] = message; sendJson(status, doc);
}

void statusApi() {
  JsonDocument doc;
  bool stationReady = WiFi.status() == WL_CONNECTED;
  doc["mode"] = "TEST";
  doc["sensorMode"] = simulatedSensors ? "simulated" : "real";
  doc["running"] = running;
  doc["relay"] = relayOn;
  doc["pidDuty"] = pidDuty;
  doc["cutoff"] = upperCutoff;
  doc["ready"] = (apReady || stationReady) && sensors[0].valid && sensors[1].valid && !faultLatched;
  doc["target"] = TARGET_C;
  doc["fault"] = faultLatched ? faultText : "";
  doc["bootCount"] = bootCount;
  doc["resetReason"] = resetReason;
  doc["interrupted"] = interrupted;
  doc["ntpSynced"] = ntpSynced;
  doc["workshopConfigured"] = WORKSHOP_WIFI_SSID[0] != '\0';
  doc["workshopConnected"] = stationReady;
  doc["stationIp"] = stationReady ? WiFi.localIP().toString() : "";
  doc["apIp"] = apReady ? WiFi.softAPIP().toString() : "";
  doc["localName"] = stationReady && mdnsReady ? String(MDNS_HOST) + ".local" : "";
  doc["outageUpperBoundSec"] = outageUpperBoundSec;
  doc["uptimeSec"] = millis() / 1000;
  doc["uptimeMs"] = millis();
  if (isfinite(chipTemperatureC)) doc["chipTemperatureC"] = chipTemperatureC;
  else doc["chipTemperatureC"] = nullptr;
  doc["sampleAtMs"] = sampleAtMs;
  doc["historyLatestSeq"] = historySeq;
  JsonArray arr = doc["sensors"].to<JsonArray>();
  for (const auto &s : sensors) {
    JsonObject item = arr.add<JsonObject>();
    if (s.valid) item["c"] = s.c; else item["c"] = nullptr;
    item["valid"] = s.valid; item["faultCode"] = s.fault;
  }
  sendJson(200, doc);
}

void logsApi() {
  JsonDocument doc;
  JsonArray arr = doc["events"].to<JsonArray>();
  for (size_t i = 0; i < eventCount; ++i) {
    size_t index = (eventHead + EVENT_CAP - eventCount + i) % EVENT_CAP;
    JsonObject item = arr.add<JsonObject>();
    item["seq"] = events[index].seq;
    if (events[index].utc) item["utc"] = events[index].utc;
    else item["utc"] = nullptr;
    item["level"] = events[index].level;
    item["message"] = events[index].message;
    item["saved"] = events[index].saved;
  }
  doc["capacity"] = EVENT_CAP;
  doc["savedCapacity"] = PERSIST_CAP;
  sendJson(200, doc);
}

void historyApi() {
  JsonDocument doc;
  doc["bootCount"] = bootCount;
  doc["capacity"] = HISTORY_CAP;
  doc["intervalMs"] = HISTORY_SAMPLE_MS;
  doc["oldestSeq"] = historyCount ? historySamples[(historyHead + HISTORY_CAP - historyCount) % HISTORY_CAP].seq : 0;
  doc["latestSeq"] = historySeq;
  JsonArray arr = doc["samples"].to<JsonArray>();
  size_t first = 0, end = historyCount;
  if (server.hasArg("before")) {
    uint32_t before = strtoul(server.arg("before").c_str(), nullptr, 10);
    while (end > 0 && historySamples[(historyHead + HISTORY_CAP - historyCount + end - 1) % HISTORY_CAP].seq >= before) --end;
    first = end > HISTORY_PAGE ? end - HISTORY_PAGE : 0;
  } else if (server.hasArg("tail")) {
    first = end > HISTORY_PAGE ? end - HISTORY_PAGE : 0;
  } else {
    uint32_t after = server.hasArg("after") ? strtoul(server.arg("after").c_str(), nullptr, 10) : 0;
    while (first < end && historySamples[(historyHead + HISTORY_CAP - historyCount + first) % HISTORY_CAP].seq <= after) ++first;
    if (end - first > HISTORY_PAGE) end = first + HISTORY_PAGE;
  }
  for (size_t i = first; i < end; ++i) {
    const HistorySample &sample = historySamples[(historyHead + HISTORY_CAP - historyCount + i) % HISTORY_CAP];
    JsonArray row = arr.add<JsonArray>();
    row.add(sample.seq);
    row.add(sample.atMs);
    if (sample.flags & 1) row.add(sample.sensor1); else row.add(nullptr);
    if (sample.flags & 2) row.add(sample.sensor2); else row.add(nullptr);
    row.add(sample.duty);
    row.add(sample.flags);
  }
  doc["more"] = end < historyCount;
  doc["moreBefore"] = first > 0;
  sendJson(200, doc);
}

void modeApi() {
  JsonDocument input;
  if (deserializeJson(input, server.arg("plain")) || !input["simulated"].is<bool>()) {
    sendError(400, "Scegli sonde reali o simulate"); return;
  }
  if (running) { sendError(409, "Ferma il test prima di cambiare le sonde"); return; }
  bool requested = input["simulated"].as<bool>();
  if (requested == simulatedSensors) { statusApi(); return; }
  setRelay(false);
  simulatedSensors = requested;
  upperCutoff = false;
  pidDuty = 0;
  previousAverage = NAN;
  if (simulatedSensors) {
    simTemperature = 25.0f; simStoredHeat = 0; simSampleAt = millis();
    updateSimulatedSensors();
    logEvent("ATTENZIONE", "Sonde SIMULATE attivate: valori finti; prova solo a bassa tensione");
  } else {
    readOne(rtd1, sensors[0]); readOne(rtd2, sensors[1]);
    logEvent("INFO", "Sonde REALI attivate: lettura delle due PT100");
  }
  recordHistory(true);
  statusApi();
}

void commandApi() {
  if (!server.hasArg("plain")) { sendError(400, "Richiesta vuota"); return; }
  JsonDocument input;
  if (deserializeJson(input, server.arg("plain"))) { sendError(400, "JSON non valido"); return; }
  String action = input["action"] | "";
  if (action == "stop") { stopTest("Test interrotto manualmente dall'utente"); statusApi(); return; }
  if (action == "simUp" || action == "simDown") {
    if (!simulatedSensors) { sendError(409, "Attiva prima le sonde simulate"); return; }
    simTemperature = constrain(simTemperature + (action == "simUp" ? 1.0f : -1.0f), 24.0f, 40.0f);
    logEvent("SIMULAZIONE", "Temperatura finta regolata a " + String(simTemperature, 1) + " °C");
    sampleAndControl(); recordHistory(true); statusApi(); return;
  }
  if (action == "reset") {
    if (running) { sendError(409, "Ferma il test prima di azzerare l'allarme"); return; }
    if (!sensors[0].valid || !sensors[1].valid || sensors[0].c >= MAX_MEASURED ||
        sensors[1].c >= MAX_MEASURED || fabsf(sensors[0].c - sensors[1].c) > MAX_DELTA) {
      sendError(409, "Sonde non valide o temperature fuori limite"); return;
    }
    faultLatched = false; faultText = "";
    logEvent("INFO", "Allarme azzerato dall'utente"); statusApi(); return;
  }
  if (action == "start") {
    if (running) { sendError(409, "Test gia in corso"); return; }
    if (faultLatched || !sensors[0].valid || !sensors[1].valid) {
      sendError(409, "Verifica le sonde e azzera gli allarmi"); return;
    }
    if (fabsf(sensors[0].c - sensors[1].c) > MAX_DELTA ||
        sensors[0].c >= MAX_MEASURED || sensors[1].c >= MAX_MEASURED) {
      sendError(409, "Temperature fuori limite"); return;
    }
    running = true; runStart = millis(); windowStart = runStart;
    pidDuty = 0; pidIntegral = 0; pidDerivative = 0; previousAverage = NAN; pidSampleAt = 0; upperCutoff = false; targetLogged = false;
    pidLogAt = millis() - 15000;
    prefs.putULong64("checkpoint", 0); // Do not reuse a timestamp from an older test.
    checkpointAt = millis() - CHECKPOINT_MS; // Save as soon as NTP is available.
    prefs.putBool("active", true);
    logEvent("INFO", String("Test PID avviato con sonde ") + (simulatedSensors ? "SIMULATE" : "REALI") +
             "; target 35 °C, stop a 36 °C");
    sampleAndControl(); recordHistory(true); statusApi(); return;
  }
  sendError(400, "Azione sconosciuta");
}

void syncTimeApi() {
  JsonDocument input;
  if (deserializeJson(input, server.arg("plain"))) { sendError(400, "JSON non valido"); return; }
  uint64_t unixMs = input["unixMs"].as<uint64_t>();
  if (unixMs < 1700000000000ULL || unixMs > 4102444800000ULL) {
    sendError(400, "Ora del dispositivo non plausibile"); return;
  }
  if (!ntpSynced) { timeKnown = true; wallAtSync = unixMs / 1000; syncAtMs = millis(); }
  statusApi();
}

void updateNetworkTime() {
  if ((uint32_t)(millis() - lastNetworkCheck) < 1000) return;
  lastNetworkCheck = millis();
  bool stationReady = WiFi.status() == WL_CONNECTED;
  if (stationReady) {
    String ip = WiFi.localIP().toString();
    if (!workshopWasConnected || workshopIp != ip) {
      workshopWasConnected = true;
      workshopIp = ip;
      if (!mdnsReady) {
        mdnsReady = MDNS.begin(MDNS_HOST);
        if (mdnsReady) MDNS.addService("http", "tcp", 80);
      }
      logEvent("RETE", "Dashboard sul Wi-Fi capannone: http://" + ip + "/" +
               (mdnsReady ? " (anche http://oven-controller.local/)" : ""));
    }
  } else if (workshopWasConnected) {
    workshopWasConnected = false;
    workshopIp = "";
    if (mdnsReady) { MDNS.end(); mdnsReady = false; }
    logEvent("ATTENZIONE", "Wi-Fi capannone disconnesso; dashboard diretta " +
             String(apReady ? "http://192.168.4.1/" : "non disponibile"));
  }
  if (stationReady && !ntpStarted) {
    configTime(0, 0, WORKSHOP_NTP_SERVER);
    ntpStarted = true;
    logEvent("INFO", "Sincronizzazione ora di rete in corso");
  }
  if (ntpStarted && !ntpSynced) {
    time_t epoch = time(nullptr);
    if (epoch >= 1700000000 && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      ntpSynced = true;
      timeKnown = true;
      logEvent("INFO", "Ora di rete sincronizzata");
      if (interrupted && previousCheckpoint && !outageLogged) {
        uint64_t bootEpoch = (uint64_t)epoch - millis() / 1000;
        if (bootEpoch >= previousCheckpoint) {
          outageUpperBoundSec = bootEpoch - previousCheckpoint;
          logEvent("ATTENZIONE", "Test interrotto al riavvio; intervallo massimo senza controllo: " + String((unsigned long)outageUpperBoundSec) + " s (non durata esatta del blackout)");
        }
        outageLogged = true;
      }
    }
  }
}

void setup() {
  // Output off before SPI or Wi-Fi initialization.
  digitalWrite(PIN_RELAY, LOW); pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RED, LOW); pinMode(PIN_RED, OUTPUT);
  digitalWrite(PIN_GREEN, LOW); pinMode(PIN_GREEN, OUTPUT);
  Serial.begin(115200);
  prefs.begin("oven-test", false);
  loadEvents();
  interrupted = prefs.getBool("active", false);
  prefs.putBool("active", false);
  previousCheckpoint = prefs.getULong64("checkpoint", 0);
  bootCount = prefs.getUInt("boots", 0) + 1; prefs.putUInt("boots", bootCount);
  resetReason = String(esp_reset_reason());
  if (interrupted) logEvent("ATTENZIONE", "Test interrotto da spegnimento o reset: rele spento; riavvio manuale richiesto");
  else logEvent("INFO", "Avvio scheda; causa reset codice " + resetReason);

  pinMode(PIN_CS1, OUTPUT); digitalWrite(PIN_CS1, HIGH);
  pinMode(PIN_CS2, OUTPUT); digitalWrite(PIN_CS2, HIGH);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
  bool rtd1Ready = rtd1.begin(MAX31865_3WIRE);
  bool rtd2Ready = rtd2.begin(MAX31865_3WIRE);
  if (!rtd1Ready || !rtd2Ready) {
    logEvent("ATTENZIONE", "MAX31865 non rilevato: verifica le Click; simulazione disponibile");
  }
  sampleAndControl();
  recordHistory(true);
  WiFi.mode(WORKSHOP_WIFI_SSID[0] ? WIFI_AP_STA : WIFI_AP);
  apReady = WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (!apReady) logEvent("ALLARME", "Access Point Wi-Fi non avviato");
  if (WORKSHOP_WIFI_SSID[0]) {
    WiFi.setAutoReconnect(true);
    WiFi.begin(WORKSHOP_WIFI_SSID, WORKSHOP_WIFI_PASSWORD);
    logEvent("RETE", "Connessione al Wi-Fi capannone in corso");
  }
  server.on("/", HTTP_GET, [] { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html; charset=utf-8", WEB_HTML); });
  server.on("/style.css", HTTP_GET, [] { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/css; charset=utf-8", WEB_CSS); });
  server.on("/app.js", HTTP_GET, [] { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "application/javascript; charset=utf-8", WEB_JS); });
  server.on("/api/status", HTTP_GET, statusApi);
  server.on("/api/logs", HTTP_GET, logsApi);
  server.on("/api/history", HTTP_GET, historyApi);
  server.on("/api/mode", HTTP_POST, modeApi);
  server.on("/api/command", HTTP_POST, commandApi);
  server.on("/api/time", HTTP_POST, syncTimeApi);
  server.onNotFound([] { sendError(404, "Percorso non trovato"); });
  server.begin();
  greenLedOn = (apReady || WiFi.status() == WL_CONNECTED) && !faultLatched;
  digitalWrite(PIN_GREEN, greenLedOn ? HIGH : LOW);
  chipTemperatureC = temperatureRead();
  lastChipTemperatureAt = millis();
  if (apReady) logEvent("RETE", "Dashboard sulla rete OvenController-Test: http://" + WiFi.softAPIP().toString() + "/");
}

void loop() {
  server.handleClient();
  updateNetworkTime();
  if ((uint32_t)(millis() - lastSample) >= SAMPLE_MS) {
    lastSample = millis();
    sampleAndControl();
    recordHistory();
  }
  applyRelayWindow();
  if (running && ntpSynced && (uint32_t)(millis() - checkpointAt) >= CHECKPOINT_MS) {
    uint64_t current = nowUtc();
    if (current > 1700000000ULL) {
      prefs.putULong64("checkpoint", current);
      checkpointAt = millis();
    }
  }
  if ((uint32_t)(millis() - lastChipTemperatureAt) >= 10000) {
    chipTemperatureC = temperatureRead();
    lastChipTemperatureAt = millis();
  }
  bool greenWanted = (apReady || workshopWasConnected) && !faultLatched;
  if (greenWanted != greenLedOn) {
    digitalWrite(PIN_GREEN, greenWanted ? HIGH : LOW);
    greenLedOn = greenWanted;
  }
  delay(10);
}
