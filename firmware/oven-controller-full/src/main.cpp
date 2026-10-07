#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_MAX31865.h>
#include <esp_system.h>
#include <esp_sntp.h>
#include <cmath>
#include <ctime>
#include "profile.h"
#include "heater_guard.h"
#include "hardware_config.h"
#include "web_assets.h"
#if __has_include("wifi_config.h")
#include "wifi_config.h"
#else
#define WORKSHOP_WIFI_SSID ""
#define WORKSHOP_WIFI_PASSWORD ""
#define WORKSHOP_NTP_SERVER "pool.ntp.org"
#endif
// ESP32-S3 N16R8; GPIO35/36 are occupied by PSRAM on this module.
constexpr int CS1 = PIN_CS1, CS2 = PIN_CS2;
constexpr int HEAT = PIN_RELAY, RED = PIN_RED, GREEN = PIN_GREEN;
constexpr float MAX_TARGET = 190.0f, HARD_LIMIT = 205.0f, MAX_DELTA = 10.0f;
constexpr float MAX_TARGET_OVERSHOOT = 8.0f;
constexpr uint32_t SAMPLE_MS = 500, HISTORY_MS = 10000;
constexpr uint32_t CHECKPOINT_MS = 30000, MAX_CYCLE_MS = 12UL * 3600000UL;
constexpr size_t MAX_RECIPES = 8, MAX_STEPS = 12, HISTORY_CAP = 4320, HISTORY_PAGE = 180;
constexpr size_t EVENT_CAP = 100, PERSIST_CAP = 40;
constexpr char AP_SSID[] = "OvenController-Full";
constexpr char AP_PASSWORD[] = "oven12345";
constexpr char MDNS_NAME[] = "oven-full";

Adafruit_MAX31865 rtd1(CS1), rtd2(CS2);
WebServer server(80);
Preferences prefs;

struct Sensor { float c = NAN, ohms = NAN; uint16_t raw = 0; uint8_t fault = 0; bool valid = false; };
struct PidGains { float kp, ki, kd; };
// Default for initial low-temperature commissioning; select the actual actuator explicitly.
struct ControlSettings { uint32_t windowMs = 60000; float maxPower = 30; bool ssr = false; };
ControlSettings controlSettings;
heater::Guard heaterGuard;
portMUX_TYPE heaterMux = portMUX_INITIALIZER_UNLOCKED;
struct HeaterNotice { bool on; heater::Fault fault; };
QueueHandle_t heaterNotices;
profile::HoldTimer holdTimer;
struct Recipe { String id, name; profile::Step steps[MAX_STEPS]; uint8_t count = 0; };
struct Event { uint32_t seq; uint64_t utc; String level, message; bool saved; };
struct Sample { uint32_t seq, ms; float t1, t2, setpoint, duty; uint8_t flags, step; };
enum class Phase : uint8_t { Idle, Running, Paused, Complete, Fault, Interrupted };
constexpr uint8_t HISTORY_START = 0x10, HISTORY_STOP = 0x20;
constexpr uint8_t HISTORY_COMPLETE = 0x40, HISTORY_FAULT = 0x80;

Sensor sensors[2];
Recipe recipes[MAX_RECIPES], activeRecipe;
size_t recipeCount = 0;
Phase phase = Phase::Idle;
bool relayOn = false, apReady = false, ntpStarted = false, ntpSynced = false;
bool mdnsReady = false, stationWasConnected = false, sensorFaultLogged = false;
bool hadInterrupted = false, checkpointWriteFaultLogged = false;
bool greenLedOn = false;
float chipTemperatureC = NAN;
uint32_t lastChipTemperatureAt = 0;
String stationIp, faultText, resetReason;
float setpointC = NAN, duty = 0, integral = 0, derivative = 0, previousMean = NAN;
float kp = 12.0f, ki = 0.015f, kd = 50.0f;
float stepStartC = NAN;
uint8_t stepIndex = 0;
uint32_t stepStarted = 0, cycleStarted = 0, pausedAt = 0, holdInBandMs = 0;
uint32_t previousControlAt = 0, windowStarted = 0, lastSampleAt = 0, lastHistoryAt = 0;
uint32_t lastNetworkCheck = 0, lastPidLog = 0, lastLagLog = 0, checkpointAt = 0;
uint32_t sampleAtMs = 0, bootCount = 0, eventSeq = 0, persistedSeq = 0;
uint64_t previousCheckpoint = 0, outageUpperBoundSec = 0;
Event events[EVENT_CAP]; size_t eventHead = 0, eventCount = 0;
Sample history[HISTORY_CAP]; size_t historyHead = 0, historyCount = 0; uint32_t historySeq = 0;

uint64_t utcNow() {
  time_t now = time(nullptr);
  return ntpSynced && now >= 1700000000 ? uint64_t(now) : 0;
}
void persistEvent(const Event &event) {
  char epoch[24];
  snprintf(epoch, sizeof(epoch), "%llu", (unsigned long long)event.utc);
  String line = String(epoch) + "|" + event.level + "|" + event.message;
  ++persistedSeq;
  prefs.putString(("log" + String(persistedSeq % PERSIST_CAP)).c_str(), line);
  prefs.putUInt("logseq", persistedSeq);
}
void logEvent(const String &level, const String &message, bool save = true) {
  Event event{++eventSeq, utcNow(), level, message, save};
  events[eventHead] = event; eventHead = (eventHead + 1) % EVENT_CAP;
  if (eventCount < EVENT_CAP) ++eventCount;
  if (save) persistEvent(event);
  Serial.printf("[%s] %s\n", level.c_str(), message.c_str());
}
void loadEvents() {
  persistedSeq = prefs.getUInt("logseq", 0);
  uint32_t first = persistedSeq > PERSIST_CAP ? persistedSeq - PERSIST_CAP + 1 : 1;
  for (uint32_t n = first; n <= persistedSeq; ++n) {
    String line = prefs.getString(("log" + String(n % PERSIST_CAP)).c_str(), "");
    int a = line.indexOf('|'), b = line.indexOf('|', a + 1);
    if (a < 0 || b < 0) continue;
    events[eventHead] = {++eventSeq, strtoull(line.substring(0, a).c_str(), nullptr, 10),
                         line.substring(a + 1, b), line.substring(b + 1), true};
    eventHead = (eventHead + 1) % EVENT_CAP;
    if (eventCount < EVENT_CAP) ++eventCount;
  }
}
bool heaterOn() {
  portENTER_CRITICAL(&heaterMux); bool on = heaterGuard.on; portEXIT_CRITICAL(&heaterMux);
  return on;
}
heater::Fault heaterFault() {
  portENTER_CRITICAL(&heaterMux); auto fault = heaterGuard.fault; portEXIT_CRITICAL(&heaterMux);
  return fault;
}
void writeHeater(bool on) {
  digitalWrite(HEAT, on == RELAY_ACTIVE_HIGH ? HIGH : LOW);
  digitalWrite(RED, on == LED_ACTIVE_HIGH ? HIGH : LOW);
}
void heaterTask(void *) {
  bool previousOn = false; heater::Fault previousFault = heater::Fault::None;
  for (;;) {
    portENTER_CRITICAL(&heaterMux);
    bool on = heaterGuard.tick(millis());
    auto fault = heaterGuard.fault;
    writeHeater(on);
    portEXIT_CRITICAL(&heaterMux);
    if (on != previousOn || fault != previousFault) {
      HeaterNotice notice{on, fault}; xQueueSend(heaterNotices, &notice, 0);
      previousOn = on; previousFault = fault;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
void inhibitHeater() {
  // ON is decided only by heaterTask from a complete, fresh control plan.
  portENTER_CRITICAL(&heaterMux); heaterGuard.inhibit(); writeHeater(false); portEXIT_CRITICAL(&heaterMux);
  relayOn = false;
}
void resetPid() {
  duty = integral = derivative = 0;
  previousMean = NAN; previousControlAt = 0; windowStarted = millis();
  inhibitHeater();
}
void recordHistory(bool force = false, uint8_t eventFlag = 0);
void stopCycle(Phase next, const String &reason) {
  inhibitHeater(); duty = 0;
  if (phase == Phase::Running || phase == Phase::Paused) {
    phase = next;
    setpointC = NAN;
    prefs.putBool("active", false);
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
  while (xQueueReceive(heaterNotices, &notice, 0) == pdTRUE)
    logEvent("USCITA", notice.on ? "Comando riscaldamento ON; LED rosso acceso" : "Comando riscaldamento OFF; LED rosso spento", false);
  relayOn = heaterOn();
  auto fault = heaterFault();
  if (fault != heater::Fault::None && phase != Phase::Fault) {
    trip(fault == heater::Fault::StaleControl ? "Controllo/letture assenti da 3 s: supervisore ha spento l'uscita" :
         fault == heater::Fault::CycleTimeout ? "Durata massima 12 ore (incluse pause): uscita spenta" :
         "Piano di controllo non valido: uscita spenta");
  }
}
void readSensor(Adafruit_MAX31865 &rtd, Sensor &sensor) {
  sensor.raw = rtd.readRTD();
  sensor.fault = rtd.readFault();
  rtd.enableBias(false);
  float reference = &sensor == &sensors[0] ? RREF_1 : RREF_2;
  sensor.ohms = sensor.raw * reference / 32768.0f;
  sensor.c = rtd.calculateTemperature(sensor.raw, RNOMINAL, reference);
  sensor.valid = sensor.fault == 0 && sensor.raw > 0 && sensor.raw < 32767 &&
                 std::isfinite(sensor.c) && sensor.c >= -20 && sensor.c <= 220;
}
String sensorMessage(const Sensor &sensor) {
  if (sensor.valid) return "Lettura valida";
  String text;
  if (sensor.fault & MAX31865_FAULT_HIGHTHRESH) text += "RTD oltre soglia alta; ";
  if (sensor.fault & MAX31865_FAULT_LOWTHRESH) text += "RTD sotto soglia bassa; ";
  if (sensor.fault & MAX31865_FAULT_REFINLOW) text += "REFIN- basso; ";
  if (sensor.fault & MAX31865_FAULT_REFINHIGH) text += "REFIN- alto / circuito aperto; ";
  if (sensor.fault & MAX31865_FAULT_RTDINLOW) text += "RTDIN- basso / circuito aperto; ";
  if (sensor.fault & MAX31865_FAULT_OVUV) text += "Sovra/sottotensione; ";
  return text.length() ? text : "Lettura fuori intervallo o SPI/sonda assente";
}
float meanC() { return (sensors[0].c + sensors[1].c) * 0.5f; }
float hottestC() { return fmaxf(sensors[0].c, sensors[1].c); }
bool sensorsHealthy() {
  return sensors[0].valid && sensors[1].valid && hottestC() < HARD_LIMIT &&
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
  stepStartC = meanC();
  holdInBandMs = 0;
  holdTimer.reset(stepStarted);
  resetPid();
  const auto &step = activeRecipe.steps[stepIndex];
  setpointC = profile::setpoint(step, stepStartC, 0);
  logEvent("CICLO", "Step " + String(stepIndex + 1) + "/" + String(activeRecipe.count) +
          " " + String(typeName(step.type)) + " verso " + String(step.target, 1) + " °C" +
          (step.type == profile::Type::Hold ? "; " + String(step.minutes) + " min in banda" :
           "; " + String(step.rate, 2) + " °C/min"));
}
void advanceStep() { ++stepIndex; beginStep(); }
void applyWindow() {
  heater::Plan plan;
  plan.cycleActive = phase == Phase::Running || phase == Phase::Paused;
  plan.heatingAllowed = phase == Phase::Running && stepIndex < activeRecipe.count &&
                        activeRecipe.steps[stepIndex].type != profile::Type::Cooldown && std::isfinite(setpointC);
  plan.healthy = sensorsHealthy(); plan.sampleAt = sampleAtMs;
  plan.cycleAt = cycleStarted; plan.windowAt = windowStarted;
  plan.windowMs = controlSettings.windowMs; plan.duty = duty;
  portENTER_CRITICAL(&heaterMux); heaterGuard.publish(plan); portEXIT_CRITICAL(&heaterMux);
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
  if (!sensors[0].valid || !sensors[1].valid) {
    if (!sensorFaultLogged) { logEvent("ATTENZIONE", "Una o entrambe le PT100 non sono leggibili"); sensorFaultLogged = true; }
    if (phase == Phase::Running || phase == Phase::Paused) trip("Sonda PT100/MAX31865 in errore: uscita spenta");
    else inhibitHeater();
    return;
  }
  if (sensorFaultLogged) { logEvent("INFO", "Entrambe le PT100 leggibili"); sensorFaultLogged = false; }
  if (hottestC() >= HARD_LIMIT) { trip("Temperatura >= 205 °C: uscita spenta"); return; }
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
void recipesApi() {
  JsonDocument doc; JsonArray list = doc["recipes"].to<JsonArray>();
  for (size_t i = 0; i < recipeCount; ++i) recipeToJson(list.add<JsonObject>(), recipes[i]);
  sendJson(200, doc);
}
bool parseRecipes(JsonVariantConst list, Recipe *out, size_t &count, String &reason) {
  if (!list.is<JsonArrayConst>()) { reason = "Elenco ricette non valido"; return false; }
  JsonArrayConst arr = list.as<JsonArrayConst>();
  if (arr.size() == 0 || arr.size() > MAX_RECIPES) { reason = "Servono 1-8 ricette"; return false; }
  count = 0;
  for (JsonVariantConst value : arr) {
    if (!value.is<JsonObjectConst>()) { reason = "Ricetta non valida"; return false; }
    JsonObjectConst item = value.as<JsonObjectConst>();
    const char *id = item["id"] | "", *name = item["name"] | "";
    size_t idLen = strlen(id), nameLen = strlen(name);
    if (!idLen || idLen > 32 || !nameLen || nameLen > 48) { reason = "ID o nome ricetta non valido"; return false; }
    for (size_t j = 0; j < idLen; ++j)
      if (!isalnum(static_cast<unsigned char>(id[j])) && id[j] != '-' && id[j] != '_') {
        reason = "ID ricetta: usa lettere, numeri, - o _"; return false;
      }
    for (size_t j = 0; j < count; ++j) if (out[j].id == id) { reason = "ID ricetta duplicato"; return false; }
    JsonArrayConst steps = item["steps"].as<JsonArrayConst>();
    if (steps.isNull() || steps.size() == 0 || steps.size() > MAX_STEPS) { reason = "Servono 1-12 step per ricetta"; return false; }
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
      if (!std::isfinite(target) || target < 0 || target > MAX_TARGET) { reason = "Target consentito: 0-190 °C"; return false; }
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
  if (phase == Phase::Running || phase == Phase::Paused) { error(409, "Ferma il ciclo prima di cambiare le ricette"); return; }
  if (server.arg("plain").length() > 16000) { error(413, "Ricette troppo grandi"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) { error(400, "JSON non valido"); return; }
  static Recipe proposed[MAX_RECIPES]; size_t count = 0; String reason;
  if (!parseRecipes(doc["recipes"], proposed, count, reason)) { error(400, reason); return; }
  JsonDocument stored; JsonArray list = stored.to<JsonArray>();
  for (size_t i = 0; i < count; ++i) recipeToJson(list.add<JsonObject>(), proposed[i]);
  String encoded; serializeJson(stored, encoded);
  if (encoded.length() > 12000 || prefs.putString("recipes", encoded) != encoded.length()) {
    error(507, "Salvataggio ricette fallito"); return;
  }
  recipeCount = count;
  for (size_t i = 0; i < count; ++i) recipes[i] = proposed[i];
  logEvent("RICETTE", "Salvate " + String(count) + " ricette");
  recipesApi();
}
void statusApi() {
  JsonDocument doc;
  bool stationReady = WiFi.status() == WL_CONNECTED;
  doc["mode"] = "FULL"; doc["firmware"] = "full-1.0"; doc["phase"] = phaseName(); doc["relay"] = heaterOn();
  doc["ready"] = (apReady || stationReady) && sensorsHealthy() && phase != Phase::Fault &&
                 heaterFault() == heater::Fault::None && uint32_t(millis() - sampleAtMs) < heater::Guard::kStaleMs;
  doc["actuator"] = controlSettings.ssr ? "ssr" : "relay";
  doc["windowSec"] = controlSettings.windowMs / 1000; doc["maxPower"] = controlSettings.maxPower;
  doc["cycleElapsedSec"] = phase == Phase::Running || phase == Phase::Paused ? uint32_t(millis() - cycleStarted) / 1000 : 0;
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
  }
  doc["fault"] = faultText; doc["bootCount"] = bootCount;
  doc["resetReason"] = resetReason;
  doc["interrupted"] = hadInterrupted;
  doc["outageUpperBoundSec"] = outageUpperBoundSec;
  doc["ntpSynced"] = ntpSynced;
  doc["utcSec"] = utcNow();
  doc["workshopConfigured"] = WORKSHOP_WIFI_SSID[0] != '\0';
  doc["workshopConnected"] = stationReady;
  doc["stationIp"] = stationReady ? WiFi.localIP().toString() : "";
  doc["apIp"] = apReady ? WiFi.softAPIP().toString() : "";
  doc["localName"] = stationReady && mdnsReady ? String(MDNS_NAME) + ".local" : "";
  doc["uptimeMs"] = millis(); doc["sampleAtMs"] = sampleAtMs;
  if (std::isfinite(chipTemperatureC)) doc["chipTemperatureC"] = chipTemperatureC;
  else doc["chipTemperatureC"] = nullptr;
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
  if (action == "stop") {
    inhibitHeater();
    if (phase == Phase::Running || phase == Phase::Paused) stopCycle(Phase::Idle, "Ciclo fermato manualmente");
    statusApi(); return;
  }
  if (action == "reset") {
    if (phase == Phase::Running || phase == Phase::Paused) { error(409, "Ferma prima il ciclo"); return; }
    readSensor(rtd1, sensors[0]); readSensor(rtd2, sensors[1]); sampleAtMs = millis();
    if (!sensorsHealthy()) { error(409, "Sonde o temperature fuori limite"); return; }
    portENTER_CRITICAL(&heaterMux); heaterGuard.clear(); writeHeater(false); portEXIT_CRITICAL(&heaterMux);
    phase = Phase::Idle; faultText = "";
    logEvent("INFO", "Allarme/interruzione riconosciuti dall'operatore"); statusApi(); return;
  }
  if (action == "pause") {
    if (phase != Phase::Running) { error(409, "Nessun ciclo da mettere in pausa"); return; }
    phase = Phase::Paused; pausedAt = millis(); inhibitHeater(); duty = 0;
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
    if (selected < 0) { error(404, "Ricetta non trovata"); return; }
    activeRecipe = recipes[selected];
    if (activeRecipe.steps[0].type == profile::Type::Ramp &&
        meanC() >= activeRecipe.steps[0].target - profile::kBand) {
      error(409, "Il forno e gia vicino/sopra il primo target: scegli una ricetta adatta"); return;
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
    item["seq"] = event.seq;
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
void updateNetwork() {
  if (uint32_t(millis() - lastNetworkCheck) < 1000) return;
  lastNetworkCheck = millis();
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    String ip = WiFi.localIP().toString();
    if (!stationWasConnected || stationIp != ip) {
      stationWasConnected = true; stationIp = ip;
      if (!mdnsReady) { mdnsReady = MDNS.begin(MDNS_NAME); if (mdnsReady) MDNS.addService("http", "tcp", 80); }
      logEvent("RETE", "Dashboard Wi-Fi capannone: http://" + ip + "/");
    }
  } else if (stationWasConnected) {
    stationWasConnected = false; stationIp = "";
    if (mdnsReady) { MDNS.end(); mdnsReady = false; }
    logEvent("ATTENZIONE", "Wi-Fi capannone disconnesso; rete diretta ancora attiva");
  }
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
        logEvent("ATTENZIONE", "Ciclo interrotto; tempo massimo senza controllo " +
                 String((unsigned long)outageUpperBoundSec) + " s (non durata esatta del blackout)");
      }
    }
  }
}
void loadRecipes() {
  static Recipe loaded[MAX_RECIPES]; size_t count = 0; String reason;
  String saved = prefs.getString("recipes", ""); JsonDocument doc;
  if (saved.length() && !deserializeJson(doc, saved) && parseRecipes(doc.as<JsonVariantConst>(), loaded, count, reason)) {
    recipeCount = count; for (size_t i = 0; i < count; ++i) recipes[i] = loaded[i]; return;
  }
  recipes[0].id = "prova-50"; recipes[0].name = "Prova a 50 C"; recipes[0].count = 3;
  recipes[0].steps[0] = {profile::Type::Ramp, 50, 1.0f, 0};
  recipes[0].steps[1] = {profile::Type::Hold, 50, 0, 10};
  recipes[0].steps[2] = {profile::Type::Cooldown, 35, 1.0f, 0};
  recipeCount = 1;
  if (saved.length()) logEvent("ALLARME", "Ricette salvate non valide: caricata la ricetta di prova");
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
  prefs.begin("oven-full", false);
  loadEvents();
  bool wasActive = prefs.getBool("active", false);
  hadInterrupted = wasActive;
  prefs.putBool("active", false);
  previousCheckpoint = prefs.getULong64("checkpoint", 0);
  bootCount = prefs.getUInt("boots", 0) + 1; prefs.putUInt("boots", bootCount);
  resetReason = String(esp_reset_reason());
  phase = wasActive ? Phase::Interrupted : Phase::Idle;
  logEvent(wasActive ? "ATTENZIONE" : "INFO", wasActive ?
           "Ciclo interrotto da spegnimento/reset: uscita spenta, riavvio solo manuale" :
           "ESP avviato; causa reset codice " + resetReason);
  loadRecipes();
  ControlSettings storedSettings{};
  if (prefs.getBytesLength("control") == sizeof(storedSettings) &&
      prefs.getBytes("control", &storedSettings, sizeof(storedSettings)) == sizeof(storedSettings) &&
      heater::validSettings(storedSettings.ssr, storedSettings.maxPower, storedSettings.windowMs)) controlSettings = storedSettings;
  logEvent("COMANDO", String(controlSettings.ssr ? "SSR" : "Rele meccanico") + "; finestra " +
           String(controlSettings.windowMs / 1000) + " s; limite potenza " + String(controlSettings.maxPower, 1) + "%");
  PidGains storedGains{};
  if (prefs.getBytesLength("pid") == sizeof(storedGains) &&
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
  WiFi.mode(WORKSHOP_WIFI_SSID[0] ? WIFI_AP_STA : WIFI_AP);
  apReady = WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (!apReady) logEvent("ALLARME", "Rete Wi-Fi diretta non avviata");
  if (WORKSHOP_WIFI_SSID[0]) {
    WiFi.setAutoReconnect(true); WiFi.begin(WORKSHOP_WIFI_SSID, WORKSHOP_WIFI_PASSWORD);
    logEvent("RETE", "Connessione Wi-Fi capannone in corso");
  }
  server.on("/", HTTP_GET, [] { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/html; charset=utf-8", WEB_HTML); });
  server.on("/style.css", HTTP_GET, [] { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "text/css; charset=utf-8", WEB_CSS); });
  server.on("/app.js", HTTP_GET, [] { server.sendHeader("Cache-Control", "no-store"); server.send_P(200, "application/javascript; charset=utf-8", WEB_JS); });
  server.on("/api/status", HTTP_GET, statusApi);
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
  server.handleClient();
  updateNetwork();
  uint32_t now = millis();
  if ((phase == Phase::Running || phase == Phase::Paused) && uint32_t(now - sampleAtMs) >= heater::Guard::kStaleMs)
    trip("Lettura sonde troppo vecchia: uscita spenta");
  if (uint32_t(now - lastSampleAt) >= SAMPLE_MS) {
    lastSampleAt = now;
    sampleAndControl(); recordHistory();
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
  bool greenWanted = (apReady || stationWasConnected) && phase != Phase::Fault;
  if (greenWanted != greenLedOn) {
    digitalWrite(GREEN, greenWanted == LED_ACTIVE_HIGH ? HIGH : LOW);
    greenLedOn = greenWanted;
  }
  delay(10);
}
