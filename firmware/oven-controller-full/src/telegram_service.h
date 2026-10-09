#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_http_client.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <ctime>
#include <new>
#include <algorithm>
#include "telegram_policy.h"
#include "report_pdf.h"

// Arduino's WiFiClientSecure library shadows the SDK's esp_crt_bundle.h.
// Use the native IDF bundle (built into this SDK), not its Arduino replacement.
extern "C" esp_err_t esp_crt_bundle_attach(void *conf);

// The worker owns all HTTPS/JSON state. The loop only copies fixed-size messages
// into PSRAM queues with zero wait. No sensor, PID, Preferences or GPIO access here.
class TelegramService {
  struct Message { uint32_t created; bool pin = false; char text[telegram::kMessageBytes]; };
  struct ReportJob {
    reports::Snapshot report;
    reportpdf::Point *points = nullptr;
    size_t count = 0;
    bool partial = false;
    uint32_t created = 0;
  };
  struct Worker {
    Message critical{}, notice{}, reply{};
    bool hasCritical = false, hasNotice = false, hasReply = false;
    char *response = nullptr;
    size_t used = 0;
    bool overflow = false;
    esp_http_client_handle_t http = nullptr;
    int64_t offset = -1;
    int64_t pinMessageId = 0;
    uint32_t nextPoll = 0, nextRequest = 0, lastSend = 0;
    unsigned failures = 0, serverRetry = 0;
    ReportJob *document = nullptr;
    char *pdf = nullptr;
    size_t pdfBytes = 0;
  };
  class PsramJson : public ArduinoJson::Allocator {
    void *allocate(size_t size) override { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
    void deallocate(void *p) override { heap_caps_free(p); }
    void *reallocate(void *p, size_t size) override { return heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  } allocator;
  static constexpr size_t kResponseBytes = 24576;
  const char *token = "";
  int64_t chatId = 0;
  Worker *worker = nullptr;
  QueueHandle_t alarms = nullptr, notices = nullptr, replies = nullptr;
  QueueHandle_t documents = nullptr;
  StaticQueue_t alarmControl{}, noticeControl{}, replyControl{};
  StaticQueue_t documentControl{};
  uint8_t documentStorage[3 * sizeof(ReportJob *)]{};
  uint8_t *alarmStorage = nullptr, *noticeStorage = nullptr, *replyStorage = nullptr;
  std::atomic<bool> available{false}, statusRequest{false}, running{false};
  std::atomic<uint32_t> sent{0}, failed{0}, dropped{0}, lastSuccess{0};
  std::atomic<int> lastHttp{0};
  std::atomic<uint32_t> reportsSent{0}, reportsDropped{0}, reportsPending{0};

  static void releaseReport(ReportJob *job) {
    if (!job) return;
    free(job->points); job->~ReportJob(); free(job);
  }
  void finishDocument(bool success) {
    releaseReport(worker->document); worker->document = nullptr;
    free(worker->pdf); worker->pdf = nullptr; worker->pdfBytes = 0;
    --reportsPending;
    if (success) ++reportsSent; else ++reportsDropped;
  }
  static void renderYield() { vTaskDelay(pdMS_TO_TICKS(1)); }

  static bool due(uint32_t now, uint32_t deadline) { return int32_t(now - deadline) >= 0; }
  static esp_err_t httpEvent(esp_http_client_event_t *event) {
    auto *w = static_cast<Worker *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
      const size_t bytes = size_t(event->data_len);
      if (w->used + bytes >= kResponseBytes) w->overflow = true;
      else if (!w->overflow) { memcpy(w->response + w->used, event->data, bytes); w->used += bytes; }
    }
    return ESP_OK;
  }
  bool request(const char *method, const String &body, JsonDocument &response) {
    // TLS allocations use internal heap in this SDK. Leave room for control/web.
    if (ESP.getFreeHeap() < 60000 || heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 32768) {
      // A retained TLS session may itself occupy this margin. Release it before
      // retrying the check, so low heap cannot permanently trap that session.
      closeHttp();
      if (ESP.getFreeHeap() < 60000 || heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 32768) {
        lastHttp = -2; return false;
      }
    }
    auto &w = *worker;
    w.used = 0; w.overflow = false; w.serverRetry = 0;
    String url = String("https://api.telegram.org/bot") + token + "/" + method;
    if (!w.http) {
      esp_http_client_config_t config{};
      config.url = url.c_str(); config.timeout_ms = 5000;
      config.crt_bundle_attach = esp_crt_bundle_attach;
      config.disable_auto_redirect = true;
      config.event_handler = httpEvent; config.user_data = &w;
      config.keep_alive_enable = true; config.buffer_size = 1024; config.buffer_size_tx = 1024;
      w.http = esp_http_client_init(&config);
      if (!w.http) { lastHttp = -2; return false; }
    }
    esp_http_client_set_url(w.http, url.c_str());
    esp_http_client_set_method(w.http, HTTP_METHOD_POST);
    esp_http_client_set_header(w.http, "Content-Type", "application/json");
    esp_http_client_set_post_field(w.http, body.c_str(), body.length());
    const esp_err_t result = esp_http_client_perform(w.http);
    const int code = result == ESP_OK ? esp_http_client_get_status_code(w.http) : -1;
    lastHttp = code;
    // The body is a local String; don't leave a dangling pointer in a reused client.
    esp_http_client_set_post_field(w.http, nullptr, 0);
    if (result != ESP_OK) { closeHttp(); return false; }
    w.response[w.used] = '\0';
    if (w.overflow) {
      // A large photo/reply/foreign message must not jam command reception.
      // The bounded prefix still contains update_id. Parse only that field and
      // deliberately skip this oversized update; never execute a partial command.
      JsonDocument ids(&allocator), onlyIds(&allocator);
      onlyIds["result"][0]["update_id"] = true;
      deserializeJson(ids, w.response, DeserializationOption::Filter(onlyIds));
      const JsonVariantConst id = ids["result"][0]["update_id"];
      if (code != 200 || strcmp(method, "getUpdates") || !id.is<int64_t>() || id.as<int64_t>() < 0) return false;
      response["ok"] = true; response["result"][0]["update_id"] = id.as<int64_t>();
      ++dropped; return true;
    }
    JsonDocument filter(&allocator);
    filter["ok"] = true; filter["parameters"]["retry_after"] = true;
    filter["result"][0]["update_id"] = true;
    filter["result"][0]["message"]["date"] = true;
    filter["result"][0]["message"]["text"] = true;
    filter["result"][0]["message"]["chat"]["id"] = true;
    filter["result"][0]["message"]["chat"]["type"] = true;
    filter["result"][0]["message"]["from"]["id"] = true;
    // sendMessage's result is an object; retain only its ID for guide pinning.
    if (strcmp(method, "sendMessage") == 0) {
      filter.remove("result"); filter["result"]["message_id"] = true;
    }
    const auto error = deserializeJson(response, w.response, DeserializationOption::Filter(filter));
    if (error) return false;
    w.serverRetry = response["parameters"]["retry_after"] | 0U;
    return code == 200 && response["ok"].as<bool>();
  }
  void closeHttp() {
    if (worker->http) { esp_http_client_cleanup(worker->http); worker->http = nullptr; }
  }
  void backoff() {
    ++failed;
    worker->nextRequest = millis() + telegram::retryMs(++worker->failures, worker->serverRetry);
  }
  bool sendMessage(const Message &message, bool critical) {
    JsonDocument body(&allocator), response(&allocator);
    body["chat_id"] = chatId; body["text"] = message.text;
    body["disable_notification"] = !critical;
    String payload; serializeJson(body, payload);
    if (!request("sendMessage", payload, response)) { backoff(); return false; }
    if (message.pin) worker->pinMessageId = response["result"]["message_id"] | int64_t(0);
    worker->failures = 0; worker->nextRequest = millis(); worker->lastSend = millis();
    ++sent; lastSuccess = millis(); return true;
  }
  void pinGuide() {
    JsonDocument body(&allocator), response(&allocator);
    body["chat_id"] = chatId; body["message_id"] = worker->pinMessageId;
    body["disable_notification"] = true;
    String payload; serializeJson(body, payload);
    if (!request("pinChatMessage", payload, response)) {
      // A deleted message or inaccessible chat cannot be repaired by retrying.
      if (lastHttp == 400 || lastHttp == 403) { worker->pinMessageId = 0; ++dropped; }
      backoff(); return;
    }
    worker->pinMessageId = 0; worker->failures = 0; worker->nextRequest = millis();
  }
  bool sendDocument() {
    auto &w = *worker;
    if (!w.pdf) {
      w.pdf = static_cast<char *>(ps_malloc(reportpdf::kCapacity));
      if (w.pdf) w.pdfBytes = reportpdf::generate(w.pdf, reportpdf::kCapacity, w.document->report,
        w.document->points, w.document->count, w.document->partial, renderYield);
      if (!w.pdfBytes) {
        finishDocument(false);
        notify("⚠️ PDF NON GENERATO\nMemoria report insufficiente. Il riepilogo resta disponibile dalla dashboard.", true);
        return false;
      }
    }
    // Release the polling TLS session before opening the streaming upload.
    closeHttp();
    if (!available || ESP.getFreeHeap() < 60000 ||
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 32768) {
      lastHttp = -2; backoff(); return false;
    }
    char name[128]; reportpdf::filename(w.document->report, name, sizeof(name));
    const String boundary = "OvenPdf" + String(esp_random(), HEX) + String(esp_random(), HEX);
    const String prefix = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
      String(static_cast<long long>(chatId)) + "\r\n--" + boundary +
      "\r\nContent-Disposition: form-data; name=\"disable_notification\"\r\n\r\ntrue\r\n--" + boundary +
      "\r\nContent-Disposition: form-data; name=\"caption\"\r\n\r\n📄 Report di fine ciclo · " +
      reportpdf::outcome(w.document->report.outcome) + "\r\n--" + boundary +
      "\r\nContent-Disposition: form-data; name=\"document\"; filename=\"" + name +
      "\"\r\nContent-Type: application/pdf\r\n\r\n";
    const String suffix = "\r\n--" + boundary + "--\r\n";
    const String url = String("https://api.telegram.org/bot") + token + "/sendDocument";
    esp_http_client_config_t config{};
    config.url = url.c_str(); config.timeout_ms = 5000;
    config.crt_bundle_attach = esp_crt_bundle_attach; config.disable_auto_redirect = true;
    config.buffer_size = 1024; config.buffer_size_tx = 1024;
    auto client = esp_http_client_init(&config);
    if (!client) { lastHttp = -2; backoff(); return false; }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", ("multipart/form-data; boundary=" + boundary).c_str());
    const uint32_t began = millis();
    bool interrupted = false;
    bool ok = esp_http_client_open(client, prefix.length() + w.pdfBytes + suffix.length()) == ESP_OK;
    const auto write = [&](const char *data, size_t bytes) {
      size_t at = 0;
      while (at < bytes && available && uint32_t(millis() - began) < 25000) {
        if (uxQueueMessagesWaiting(alarms)) { interrupted = true; return false; }
        const int n = esp_http_client_write(client, data + at, std::min(size_t(1024), bytes - at));
        if (n <= 0) return false;
        at += n; vTaskDelay(pdMS_TO_TICKS(1));
      }
      return at == bytes;
    };
    ok = ok && write(prefix.c_str(), prefix.length()) && write(w.pdf, w.pdfBytes) && write(suffix.c_str(), suffix.length());
    if (ok) ok = esp_http_client_fetch_headers(client) >= 0;
    const int code = ok ? esp_http_client_get_status_code(client) : -1;
    size_t bytes = 0;
    while (ok && !esp_http_client_is_complete_data_received(client) && bytes < kResponseBytes - 1) {
      if (!available || uint32_t(millis() - began) >= 30000) { ok = false; break; }
      const int n = esp_http_client_read(client, w.response + bytes, kResponseBytes - 1 - bytes);
      if (n < 0) { ok = false; break; }
      if (!n) { ok = esp_http_client_is_complete_data_received(client); break; }
      bytes += n;
    }
    ok = ok && esp_http_client_is_complete_data_received(client);
    esp_http_client_cleanup(client); w.response[bytes] = 0; lastHttp = code; w.serverRetry = 0;
    // A new alarm preempts the attachment; preserve the PDF without imposing
    // the upload retry delay on that alarm's notification.
    if (interrupted) { w.nextRequest = millis(); return false; }
    JsonDocument response(&allocator), filter(&allocator);
    filter["ok"] = true; filter["parameters"]["retry_after"] = true;
    if (ok && !deserializeJson(response, w.response, DeserializationOption::Filter(filter)))
      w.serverRetry = response["parameters"]["retry_after"] | 0U;
    else ok = false;
    if (!ok || code != 200 || !response["ok"].as<bool>()) { backoff(); return false; }
    w.failures = 0; w.nextRequest = millis(); w.lastSend = millis();
    ++sent; lastSuccess = millis(); finishDocument(true); return true;
  }
  void poll() {
    auto &w = *worker;
    JsonDocument body(&allocator), response(&allocator);
    body["offset"] = w.offset; body["limit"] = 1; body["timeout"] = 2;
    body["allowed_updates"].to<JsonArray>().add("message");
    String payload; serializeJson(body, payload);
    if (!request("getUpdates", payload, response)) { backoff(); return; }
    w.failures = 0; w.nextRequest = millis();
    bool found = false;
    for (JsonObjectConst update : response["result"].as<JsonArrayConst>()) {
      found = true;
      const int64_t id = update["update_id"] | int64_t(-1);
      if (id < 0 || id < w.offset) continue;
      w.offset = id + 1; // Also acknowledge foreign, expired and unsupported updates.
      const JsonObjectConst message = update["message"];
      if (!telegram::authorized(chatId, message["chat"]["id"] | int64_t(0),
          message["from"]["id"] | int64_t(0), message["chat"]["type"] | "")) continue;
      if (!telegram::fresh(message["date"] | int64_t(0), int64_t(time(nullptr)))) continue;
      switch (telegram::command(message["text"] | "")) {
        case telegram::Command::Status: statusRequest = true; break;
        case telegram::Command::Help:
          notify(telegram::kGuideText, false, strcmp(message["text"] | "", "/start") == 0);
          break;
        default: break;
      }
    }
    w.nextPoll = millis() + (found ? 200 : 2000);
  }
  void run() {
    auto &w = *worker;
    for (;;) {
      if (!available.load()) { closeHttp(); vTaskDelay(pdMS_TO_TICKS(500)); continue; }
      const uint32_t now = millis();
      if (!due(now, w.nextRequest)) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
      if (!w.hasCritical) w.hasCritical = xQueueReceive(alarms, &w.critical, 0) == pdTRUE;
      if (!w.hasReply) w.hasReply = xQueueReceive(replies, &w.reply, 0) == pdTRUE;
      if (!w.hasNotice) w.hasNotice = xQueueReceive(notices, &w.notice, 0) == pdTRUE;
      if (!w.document) xQueueReceive(documents, &w.document, 0);
      if (w.document && uint32_t(now - w.document->created) > 86400000) finishDocument(false);
      if (w.hasReply && telegram::expired(now, w.reply.created, true, false)) { w.hasReply = false; ++dropped; }
      if (w.hasNotice && telegram::expired(now, w.notice.created, false, false)) { w.hasNotice = false; ++dropped; }
      Message *out = w.hasCritical ? &w.critical : w.hasReply ? &w.reply : w.hasNotice ? &w.notice : nullptr;
      if (out && uint32_t(now - w.lastSend) >= 1100) {
        if (sendMessage(*out, out == &w.critical)) {
          if (out == &w.critical) w.hasCritical = false;
          else if (out == &w.reply) w.hasReply = false;
          else w.hasNotice = false;
        }
      }
      else if (!out && w.pinMessageId) pinGuide();
      else if (!out && w.document && uint32_t(now - w.document->created) >= 2000 && uint32_t(now - w.lastSend) >= 1100)
        sendDocument();
      // Commands are read even with a stream of notices. Polling is bounded at 2 s.
      if (due(millis(), w.nextRequest) && due(millis(), w.nextPoll)) poll();
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
  static void task(void *self) { static_cast<TelegramService *>(self)->run(); }
  static void copyText(Message &message, const String &text) {
    message.created = millis();
    size_t bytes = text.length();
    if (bytes >= sizeof(message.text)) {
      bytes = sizeof(message.text) - 1;
      // Don't split a UTF-8 codepoint at the fixed buffer boundary.
      while (bytes && (uint8_t(text[bytes]) & 0xc0) == 0x80) --bytes;
    }
    memcpy(message.text, text.c_str(), bytes); message.text[bytes] = '\0';
  }
public:
  bool configured = false;
  bool begin(const char *botToken, const char *privateChat) {
    char *end = nullptr;
    const long long chat = strtoll(privateChat, &end, 10);
    configured = telegram::validToken(botToken) && privateChat[0] && end && !end[0] && chat > 0;
    if (!configured) return false;
    token = botToken; chatId = chat;
    // No large queues or JSON response buffers in the control's internal RAM.
    worker = static_cast<Worker *>(ps_calloc(1, sizeof(Worker)));
    alarmStorage = static_cast<uint8_t *>(ps_malloc(4 * sizeof(Message)));
    noticeStorage = static_cast<uint8_t *>(ps_malloc(6 * sizeof(Message)));
    replyStorage = static_cast<uint8_t *>(ps_malloc(sizeof(Message)));
    if (worker) { new (worker) Worker(); worker->response = static_cast<char *>(ps_malloc(kResponseBytes)); }
    if (!worker || !worker->response || !alarmStorage || !noticeStorage || !replyStorage) {
      if (worker) { free(worker->response); free(worker); worker = nullptr; }
      free(alarmStorage); free(noticeStorage); free(replyStorage); return false;
    }
    alarms = xQueueCreateStatic(4, sizeof(Message), alarmStorage, &alarmControl);
    notices = xQueueCreateStatic(6, sizeof(Message), noticeStorage, &noticeControl);
    replies = xQueueCreateStatic(1, sizeof(Message), replyStorage, &replyControl);
    documents = xQueueCreateStatic(3, sizeof(ReportJob *), documentStorage, &documentControl);
    // Wi-Fi is on core 0; the Arduino loop remains on core 1. Guard priority is 2.
    running = xTaskCreatePinnedToCore(task, "telegram", 12288, this, 1, nullptr, 0) == pdPASS;
    if (!running) {
      vQueueDelete(alarms); vQueueDelete(notices); vQueueDelete(replies); vQueueDelete(documents);
      free(worker->response); free(worker); worker = nullptr;
      free(alarmStorage); free(noticeStorage); free(replyStorage);
    }
    return running;
  }
  void setAvailable(bool ready) { available = ready; }
  bool takeStatusRequest() { return running && statusRequest.exchange(false); }
  bool notify(const String &text, bool critical = false, bool pin = false) {
    if (!running) return false;
    Message message{}; copyText(message, text); message.pin = pin;
    const bool queued = xQueueSend(critical ? alarms : notices, &message, 0) == pdTRUE;
    if (!queued) ++dropped;
    return queued;
  }
  template <typename Sample>
  bool report(const reports::Snapshot &snapshot, const Sample *curve, size_t count, bool partial) {
    if (!running) return false;
    if (reportsPending >= 4 || count > 4321 || (count && !curve)) { ++reportsDropped; return false; }
    auto *job = static_cast<ReportJob *>(ps_malloc(sizeof(ReportJob)));
    if (!job) { ++reportsDropped; return false; }
    new (job) ReportJob(); job->report = snapshot; job->created = millis(); job->partial = partial;
    job->points = count ? static_cast<reportpdf::Point *>(ps_malloc(count * sizeof(reportpdf::Point))) : nullptr;
    if (count && !job->points) { releaseReport(job); ++reportsDropped; return false; }
    job->count = count;
    for (size_t i = 0; i < count; ++i) job->points[i] = {curve[i].ms,
      (curve[i].flags & 3) == 3 ? (curve[i].t1 + curve[i].t2) * 0.5f : NAN,
      curve[i].flags & 8 ? curve[i].setpoint : NAN};
    ++reportsPending;
    if (xQueueSend(documents, &job, 0) != pdTRUE) { --reportsPending; releaseReport(job); ++reportsDropped; return false; }
    return true;
  }
  void reply(const String &text) {
    if (!running) return;
    Message message{}; copyText(message, text); xQueueOverwrite(replies, &message);
  }
  void status(JsonObject object) const {
    object["configured"] = configured; object["running"] = running.load();
    object["networkReady"] = available.load(); object["sent"] = sent.load();
    object["failedAttempts"] = failed.load(); object["dropped"] = dropped.load();
    object["lastHttpCode"] = lastHttp.load(); object["lastSentAtMs"] = lastSuccess.load();
    object["reportsSent"] = reportsSent.load(); object["reportsPending"] = reportsPending.load();
    object["reportsDropped"] = reportsDropped.load();
  }
};
