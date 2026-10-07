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
#include "hardware_config.h"
#include "output_test.h"
#include "web_assets.h"
#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
#define WORKSHOP_WIFI_SSID ""
#define WORKSHOP_WIFI_PASSWORD ""
#endif

constexpr char AP_SSID[] = "OvenController-FieldTest";
constexpr char AP_PASSWORD[] = "oven12345";
constexpr char MDNS_HOST[] = "oven-field-test";
constexpr uint32_t SAMPLE_MS = 500, HISTORY_MS = 1000;
constexpr size_t HISTORY_CAP = 3600, HISTORY_PAGE = 300, LOG_CAP = 128;
Adafruit_MAX31865 rtd1(PIN_CS1), rtd2(PIN_CS2);
WebServer server(80);
Preferences prefs;
OutputTest outputs;
portMUX_TYPE outputMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t ownerToken = 0;
bool serverReady = false, apReady = false, mdnsReady = false;
bool wasConnected = false, interrupted = false, activeSaved = false;
uint32_t bootCount = 0, lastSample = 0, sampleAt = 0, lastHistory = 0, lastNetwork = 0, lastChip = 0;
float chipC = NAN;
String resetReason, lastStationIp;

struct Sensor { float c = NAN, ohms = NAN; uint16_t raw = 0; uint8_t fault = 0; bool valid = false; };
Sensor sensors[2];
struct Event { uint32_t seq, at; String level, message; };
Event events[LOG_CAP];
size_t eventHead = 0, eventCount = 0;
uint32_t eventSeq = 0;
struct History { uint32_t seq, at; float c1, c2; uint16_t raw1, raw2; uint8_t fault1, fault2, flags; };
History history[HISTORY_CAP];
size_t historyHead = 0, historyCount = 0;
uint32_t historySeq = 0;
struct OutputNotice { TestEvent event; TestMode previous; bool relay, red, green; };
QueueHandle_t outputNotices;

const char *modeName(TestMode mode) {
  switch (mode) {
    case TestMode::RelayPulse: return "Impulso rele";
    case TestMode::RelaySequence: return "Rele: 3 impulsi";
    case TestMode::LedGreen: return "LED verde";
    case TestMode::LedRed: return "LED rosso";
    case TestMode::LedBoth: return "Entrambi i LED";
    case TestMode::LedOff: return "LED spenti";
    case TestMode::LedSequence: return "Sequenza LED";
    default: return "Lettura sonde";
  }
}

void logEvent(const String &level, const String &message) {
  events[eventHead] = {++eventSeq, millis(), level, message};
  eventHead = (eventHead + 1) % LOG_CAP;
  if (eventCount < LOG_CAP) ++eventCount;
  Serial.printf("[%lu ms][%s] %s\n", (unsigned long)millis(), level.c_str(), message.c_str());
}

OutputTest outputSnapshot() {
  portENTER_CRITICAL(&outputMux);
  OutputTest result = outputs;
  portEXIT_CRITICAL(&outputMux);
  return result;
}

void writeOutputs() {
  // Caller holds outputMux. GPIO4 is driven only by this supervisor/STOP path.
  digitalWrite(PIN_RELAY, outputs.relay == RELAY_ACTIVE_HIGH ? HIGH : LOW);
  digitalWrite(PIN_RED, outputs.red == LED_ACTIVE_HIGH ? HIGH : LOW);
  digitalWrite(PIN_GREEN, outputs.green == LED_ACTIVE_HIGH ? HIGH : LOW);
}

void outputTask(void *) {
  bool previousRelay = false, previousRed = false, previousGreen = false;
  for (;;) {
    portENTER_CRITICAL(&outputMux);
    TestMode previousMode = outputs.mode;
    TestEvent event = outputs.tick(millis(), serverReady);
    if (!outputs.armed) ownerToken = 0;
    writeOutputs();
    OutputNotice notice{event, previousMode, outputs.relay, outputs.red, outputs.green};
    portEXIT_CRITICAL(&outputMux);
    if (event != TestEvent::None || notice.relay != previousRelay || notice.red != previousRed || notice.green != previousGreen) {
      xQueueSend(outputNotices, &notice, 0);
      previousRelay = notice.relay; previousRed = notice.red; previousGreen = notice.green;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void stopOutputs(const String &why) {
  portENTER_CRITICAL(&outputMux);
  outputs.stop(); ownerToken = 0; writeOutputs();
  portEXIT_CRITICAL(&outputMux);
  prefs.putBool("active", false); activeSaved = false;
  logEvent("STOP", why);
}

void drainOutputNotices() {
  OutputNotice notice;
  while (xQueueReceive(outputNotices, &notice, 0) == pdTRUE) {
    if (notice.event == TestEvent::Completed) logEvent("TEST", String(modeName(notice.previous)) + ": terminato automaticamente");
    if (notice.event == TestEvent::ConnectionLost) logEvent("STOP", "Heartbeat assente da 2,5 s: test fermato e comandi disabilitati");
    if (notice.event == TestEvent::ArmExpired) logEvent("STOP", "Abilitazione scaduta dopo 60 s: test fermato");
    logEvent("GPIO", String("Comando rele ") + (notice.relay ? "ON" : "OFF") +
             "; rosso " + (notice.red ? "ON" : "OFF") + "; verde " + (notice.green ? "ON" : "OFF"));
  }
  if (activeSaved && !outputSnapshot().armed) { prefs.putBool("active", false); activeSaved = false; }
}

String sensorMessage(const Sensor &s) {
  if (s.valid) return "Lettura valida";
  String text;
  if (s.fault & MAX31865_FAULT_HIGHTHRESH) text += "RTD oltre soglia alta; ";
  if (s.fault & MAX31865_FAULT_LOWTHRESH) text += "RTD sotto soglia bassa; ";
  if (s.fault & MAX31865_FAULT_REFINLOW) text += "REFIN- basso; ";
  if (s.fault & MAX31865_FAULT_REFINHIGH) text += "REFIN- alto / circuito aperto; ";
  if (s.fault & MAX31865_FAULT_RTDINLOW) text += "RTDIN- basso / circuito aperto; ";
  if (s.fault & MAX31865_FAULT_OVUV) text += "Sovra/sottotensione MAX31865; ";
  if (s.fault && text.isEmpty()) text = "Fault MAX31865 non riconosciuto";
  if (!s.fault) text = "Dato fuori intervallo (-50..220 °C) o SPI/sonda assente: controlla alimentazione, fili e CS";
  return text;
}

void readSensor(Adafruit_MAX31865 &rtd, Sensor &s, float reference) {
  const bool wasValid = s.valid;
  const uint8_t previousFault = s.fault;
  // One conversion for raw, resistance and temperature. No simulated values.
  s.raw = rtd.readRTD();
  s.fault = rtd.readFault();
  rtd.enableBias(false);
  s.ohms = s.raw * reference / 32768.0f;
  s.c = rtd.calculateTemperature(s.raw, RNOMINAL, reference);
  s.valid = s.fault == 0 && s.raw > 0 && s.raw < 32767 && isfinite(s.c) && s.c >= -50 && s.c <= 220;
  if (s.valid != wasValid || s.fault != previousFault) {
    logEvent(s.valid ? "PT100" : "SONDA", String(&s == &sensors[0] ? "PT100 #1: " : "PT100 #2: ") + sensorMessage(s));
  }
}

void recordHistory() {
  const uint32_t now = millis();
  if (historyCount && uint32_t(now - lastHistory) < HISTORY_MS) return;
  lastHistory = now;
  const auto state = outputSnapshot();
  const uint8_t flags = (sensors[0].valid ? 1 : 0) | (sensors[1].valid ? 2 : 0) |
                        (state.relay ? 4 : 0) | (state.red ? 8 : 0) | (state.green ? 16 : 0);
  history[historyHead] = {++historySeq, sampleAt, sensors[0].c, sensors[1].c,
                         sensors[0].raw, sensors[1].raw, sensors[0].fault, sensors[1].fault, flags};
  historyHead = (historyHead + 1) % HISTORY_CAP;
  if (historyCount < HISTORY_CAP) ++historyCount;
}

void sendJson(int code, const JsonDocument &doc) {
  String body; serializeJson(doc, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", body);
}
void sendError(int code, const char *message) { JsonDocument doc; doc["error"] = message; sendJson(code, doc); }

void statusApi() {
  auto state = outputSnapshot();
  JsonDocument doc;
  const uint32_t now = millis();
  doc["firmware"] = "field-test-1.0"; doc["bootCount"] = bootCount;
  doc["uptimeMs"] = now; doc["sampleAtMs"] = sampleAt;
  doc["resetReason"] = resetReason; doc["interrupted"] = interrupted;
  doc["armed"] = state.armed; doc["armRemainingMs"] = state.armRemaining(now);
  doc["running"] = state.mode != TestMode::Idle; doc["test"] = modeName(state.mode);
  doc["remainingMs"] = state.remaining(now);
  doc["relay"] = state.relay; doc["red"] = state.red; doc["green"] = state.green;
  doc["leaseMs"] = OutputTest::LEASE_MS;
  doc["workshopConfigured"] = WORKSHOP_WIFI_SSID[0] != '\0';
  doc["workshopConnected"] = WiFi.status() == WL_CONNECTED;
  doc["stationIp"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
  doc["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  doc["apIp"] = apReady ? WiFi.softAPIP().toString() : "";
  doc["localName"] = mdnsReady ? String(MDNS_HOST) + ".local" : "";
  doc["freeHeap"] = ESP.getFreeHeap(); doc["minFreeHeap"] = ESP.getMinFreeHeap();
  if (isfinite(chipC)) doc["chipTemperatureC"] = chipC; else doc["chipTemperatureC"] = nullptr;
  doc["historyLatestSeq"] = historySeq;
  JsonArray arr = doc["sensors"].to<JsonArray>();
  for (size_t i = 0; i < 2; ++i) {
    const Sensor &s = sensors[i];
    auto item = arr.add<JsonObject>();
    item["valid"] = s.valid;
    if (s.valid) item["c"] = s.c; else item["c"] = nullptr;
    item["ohms"] = s.ohms; item["raw"] = s.raw; item["faultCode"] = s.fault;
    item["message"] = sensorMessage(s); item["rref"] = i ? RREF_2 : RREF_1;
  }
  sendJson(200, doc);
}

void commandApi() {
  JsonDocument input;
  if (deserializeJson(input, server.arg("plain")) || !input["action"].is<const char *>()) {
    sendError(400, "Richiesta JSON non valida"); return;
  }
  String action = input["action"].as<String>();
  if (action == "stop") { stopOutputs("STOP manuale: uscita spenta, test disabilitati"); statusApi(); return; }
  if (action == "arm") {
    if (!input["loadsDisconnected"].is<bool>() || !input["loadsDisconnected"].as<bool>()) {
      sendError(400, "Conferma che le resistenze reali siano scollegate"); return;
    }
    const uint32_t token = esp_random() | 1U;
    portENTER_CRITICAL(&outputMux);
    const bool busy = outputs.armed;
    if (!busy) { outputs.arm(millis()); ownerToken = token; writeOutputs(); }
    portEXIT_CRITICAL(&outputMux);
    if (busy) { sendError(409, "Test gia abilitati da una dashboard: premi STOP per liberarli"); return; }
    prefs.putBool("active", true); activeSaved = true;
    logEvent("TEST", "Test abilitati per 60 s con resistenze dichiarate scollegate; heartbeat richiesto");
    JsonDocument reply; reply["token"] = token; sendJson(200, reply); return;
  }
  TestMode mode = TestMode::Idle;
  if (action == "relayPulse") mode = TestMode::RelayPulse;
  else if (action == "relaySequence") mode = TestMode::RelaySequence;
  else if (action == "ledGreen") mode = TestMode::LedGreen;
  else if (action == "ledRed") mode = TestMode::LedRed;
  else if (action == "ledBoth") mode = TestMode::LedBoth;
  else if (action == "ledOff") mode = TestMode::LedOff;
  else if (action == "ledSequence") mode = TestMode::LedSequence;
  else { sendError(400, "Azione sconosciuta"); return; }
  uint32_t duration = 1000;
  if (mode == TestMode::RelayPulse) {
    if (!input["durationMs"].is<uint32_t>()) { sendError(400, "Durata intera richiesta (100..5000 ms)"); return; }
    duration = input["durationMs"].as<uint32_t>();
    if (duration < 100 || duration > 5000) { sendError(400, "Durata ammessa 100..5000 ms"); return; }
  }
  if (!input["token"].is<uint32_t>()) { sendError(403, "Abilita prima i test da questa dashboard"); return; }
  const uint32_t token = input["token"].as<uint32_t>();
  portENTER_CRITICAL(&outputMux);
  const bool owned = token != 0 && token == ownerToken && outputs.armed;
  const bool started = owned && outputs.start(mode, millis(), duration);
  if (started) writeOutputs();
  portEXIT_CRITICAL(&outputMux);
  if (!owned) { sendError(403, "Abilitazione assente, scaduta o appartenente a un'altra dashboard"); return; }
  if (!started) { sendError(409, "Test in corso o abilitazione scaduta: attendi o premi STOP"); return; }
  logEvent("TEST", String(modeName(mode)) + (mode == TestMode::RelayPulse ? " " + String(duration) + " ms" : "") +
           "; nessun feedback fisico dell'uscita");
  statusApi();
}

void heartbeatApi() {
  JsonDocument input;
  if (deserializeJson(input, server.arg("plain")) || !input["token"].is<uint32_t>()) { sendError(400, "Token richiesto"); return; }
  uint32_t token = input["token"].as<uint32_t>();
  portENTER_CRITICAL(&outputMux);
  bool ok = token != 0 && token == ownerToken && outputs.heartbeat(millis());
  portEXIT_CRITICAL(&outputMux);
  if (!ok) { sendError(403, "Abilitazione scaduta: test fermati"); return; }
  server.sendHeader("Cache-Control", "no-store"); server.send(204);
}

void logsApi() {
  JsonDocument doc; auto arr = doc["events"].to<JsonArray>();
  for (size_t i = 0; i < eventCount; ++i) {
    const auto &e = events[(eventHead + LOG_CAP - eventCount + i) % LOG_CAP];
    auto item = arr.add<JsonObject>(); item["seq"] = e.seq; item["atMs"] = e.at;
    item["level"] = e.level; item["message"] = e.message;
  }
  doc["bootCount"] = bootCount; doc["capacity"] = LOG_CAP; sendJson(200, doc);
}

void historyApi() {
  JsonDocument doc; doc["bootCount"] = bootCount; doc["capacity"] = HISTORY_CAP;
  doc["intervalMs"] = HISTORY_MS; doc["latestSeq"] = historySeq;
  auto arr = doc["samples"].to<JsonArray>();
  size_t first = 0, end = historyCount;
  if (server.hasArg("tail")) first = end > HISTORY_PAGE ? end - HISTORY_PAGE : 0;
  else {
    uint32_t after = server.hasArg("after") ? strtoul(server.arg("after").c_str(), nullptr, 10) : 0;
    while (first < end && history[(historyHead + HISTORY_CAP - historyCount + first) % HISTORY_CAP].seq <= after) ++first;
    if (end - first > HISTORY_PAGE) end = first + HISTORY_PAGE;
  }
  for (size_t i = first; i < end; ++i) {
    const auto &s = history[(historyHead + HISTORY_CAP - historyCount + i) % HISTORY_CAP];
    auto row = arr.add<JsonArray>(); row.add(s.seq); row.add(s.at);
    if (s.flags & 1) row.add(s.c1); else row.add(nullptr);
    if (s.flags & 2) row.add(s.c2); else row.add(nullptr);
    row.add(s.raw1); row.add(s.raw2); row.add(s.fault1); row.add(s.fault2); row.add(s.flags);
  }
  doc["more"] = end < historyCount; sendJson(200, doc);
}

void updateNetwork() {
  if (uint32_t(millis() - lastNetwork) < 1000) return;
  lastNetwork = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && (!wasConnected || lastStationIp != WiFi.localIP().toString())) {
    lastStationIp = WiFi.localIP().toString();
    if (!mdnsReady) { mdnsReady = MDNS.begin(MDNS_HOST); if (mdnsReady) MDNS.addService("http", "tcp", 80); }
    logEvent("RETE", "Dashboard capannone: http://" + lastStationIp + "/");
  }
  if (!connected && wasConnected) {
    if (mdnsReady) { MDNS.end(); mdnsReady = false; }
    logEvent("RETE", "Wi-Fi capannone disconnesso; rete diretta disponibile");
  }
  wasConnected = connected;
  portENTER_CRITICAL(&outputMux); serverReady = apReady || connected; portEXIT_CRITICAL(&outputMux);
}

void setup() {
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_HIGH ? LOW : HIGH); pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RED, LED_ACTIVE_HIGH ? LOW : HIGH); pinMode(PIN_RED, OUTPUT);
  digitalWrite(PIN_GREEN, LED_ACTIVE_HIGH ? LOW : HIGH); pinMode(PIN_GREEN, OUTPUT);
  Serial.begin(115200);
  outputNotices = xQueueCreate(16, sizeof(OutputNotice));
  if (!outputNotices || xTaskCreate(outputTask, "output-test", 3072, nullptr, 2, nullptr) != pdPASS) {
    Serial.println("Supervisore uscite non avviato: firmware fermo, rele spento");
    for (;;) delay(1000);
  }
  prefs.begin("oven-field", false);
  interrupted = prefs.getBool("active", false); prefs.putBool("active", false);
  bootCount = prefs.getUInt("boots", 0) + 1; prefs.putUInt("boots", bootCount);
  resetReason = String(esp_reset_reason());
  logEvent("AVVIO", "Firmware diagnostico; rele spento; reset codice " + resetReason);
  if (interrupted) logEvent("STOP", "Abilitazione/test interrotto da reset o alimentazione: nessuna ripresa automatica");
  pinMode(PIN_CS1, OUTPUT); digitalWrite(PIN_CS1, HIGH);
  pinMode(PIN_CS2, OUTPUT); digitalWrite(PIN_CS2, HIGH);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
  rtd1.begin(MAX31865_3WIRE); rtd2.begin(MAX31865_3WIRE);
  rtd1.enable50Hz(true); rtd2.enable50Hz(true);
  logEvent("PT100", "MAX31865 configurati: 3 fili, filtro 50 Hz, RREF " + String(RREF_1, 1) + "/" + String(RREF_2, 1) + " ohm; la validita viene dalle letture");
  WiFi.mode(WORKSHOP_WIFI_SSID[0] ? WIFI_AP_STA : WIFI_AP);
  apReady = WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (apReady) logEvent("RETE", String(AP_SSID) + ": http://" + WiFi.softAPIP().toString() + "/");
  else logEvent("ERRORE", "Access Point non avviato");
  if (WORKSHOP_WIFI_SSID[0]) { WiFi.setAutoReconnect(true); WiFi.begin(WORKSHOP_WIFI_SSID, WORKSHOP_WIFI_PASSWORD); }
  server.on("/", HTTP_GET, [] { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html; charset=utf-8", WEB_HTML); });
  server.on("/style.css", HTTP_GET, [] { server.send_P(200, "text/css; charset=utf-8", WEB_CSS); });
  server.on("/app.js", HTTP_GET, [] { server.send_P(200, "application/javascript; charset=utf-8", WEB_JS); });
  server.on("/api/status", HTTP_GET, statusApi); server.on("/api/command", HTTP_POST, commandApi);
  server.on("/api/heartbeat", HTTP_POST, heartbeatApi);
  server.on("/api/logs", HTTP_GET, logsApi); server.on("/api/history", HTTP_GET, historyApi);
  server.onNotFound([] { sendError(404, "Percorso non trovato"); });
  server.begin();
  portENTER_CRITICAL(&outputMux); serverReady = apReady || WiFi.status() == WL_CONNECTED; portEXIT_CRITICAL(&outputMux);
  chipC = temperatureRead(); lastChip = millis();
}

void loop() {
  // GPIO deadlines run independently in outputTask, including during slow HTTP/SPI operations.
  drainOutputNotices();
  server.handleClient(); updateNetwork();
  if (!lastSample || uint32_t(millis() - lastSample) >= SAMPLE_MS) {
    lastSample = millis();
    readSensor(rtd1, sensors[0], RREF_1); readSensor(rtd2, sensors[1], RREF_2);
    sampleAt = millis(); recordHistory();
  }
  if (uint32_t(millis() - lastChip) >= 10000) { chipC = temperatureRead(); lastChip = millis(); }
  delay(5);
}
