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
#include "telegram_delivery.h"
#include "telegram_runtime.h"
#include "report_pdf.h"
#include "plot_png.h"
#include "plot_snapshot.h"

// Arduino's WiFiClientSecure library shadows the SDK's esp_crt_bundle.h.
// Use the native IDF bundle (built into this SDK), not its Arduino replacement.
extern "C" esp_err_t esp_crt_bundle_attach(void *conf);

// The worker owns all HTTPS/JSON state. The loop only copies fixed-size messages
// into PSRAM queues with zero wait. No sensor, PID, Preferences or GPIO access here.
class TelegramService {
  struct Message { uint32_t created; bool pin = false, menu = false, event = false; telegram::EventScope scope = telegram::EventScope::Selected; char text[telegram::kMessageBytes]; };
  struct ReportJob {
    reports::Snapshot report;
    reportpdf::Point *points = nullptr;
    size_t count = 0;
    bool partial = false, plot = false;
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
    uint32_t lastSend = 0;
    unsigned serverRetry = 0;
    std::atomic<bool> tlsConnected{false};
    telegram::RetryLane messages, polling, attachments, setup;
    bool commandsSet = false;
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
  std::atomic<bool> available{false}, statusRequest{false}, plotRequest{false}, running{false};
  std::atomic<int> modeValue{int(telegram::Mode::Brief)}, requestedMode{-1};
  std::atomic<uint32_t> sent{0}, failed{0}, dropped{0}, lastSuccess{0};
  std::atomic<int> lastHttp{0};
  std::atomic<uint32_t> plotsSent{0}, plotsDropped{0}, plotsPending{0};
  std::atomic<uint32_t> reportsSent{0}, reportsDropped{0}, reportsPending{0};
  std::atomic<uint32_t> reportsUnconfirmed{0}, duplicateReportsSkipped{0};
  std::atomic<uint32_t> internalFree{0}, largestInternalBlock{0}, memoryDeferrals{0};
  std::atomic<uint32_t> pollCount{0}, lastPoll{0}, commandsReceived{0}, staleCommands{0};
  telegram::RecentReportCycles queuedCycles;
#if defined(CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN) && defined(CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN)
  static constexpr size_t kTlsRecordBytes = CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN > CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN ?
    CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN : CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN;
#elif defined(CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN)
  static constexpr size_t kTlsRecordBytes = CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN;
#else
  static constexpr size_t kTlsRecordBytes = 16384;
#endif

  static void releaseReport(ReportJob *job) {
    if (!job) return;
    free(job->points); job->~ReportJob(); free(job);
  }
  void finishDocument(bool success) {
    const bool plot = worker->document->plot;
    releaseReport(worker->document); worker->document = nullptr;
    free(worker->pdf); worker->pdf = nullptr; worker->pdfBytes = 0;
    if (plot) { --plotsPending; if (success) ++plotsSent; else ++plotsDropped; }
    else { --reportsPending; if (success) ++reportsSent; else ++reportsDropped; }
  }
  static void renderYield() { vTaskDelay(pdMS_TO_TICKS(1)); }

  static esp_err_t httpEvent(esp_http_client_event_t *event) {
    auto *w = static_cast<Worker *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_CONNECTED) w->tlsConnected = true;
    if (event->event_id == HTTP_EVENT_DISCONNECTED || event->event_id == HTTP_EVENT_ERROR) w->tlsConnected = false;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
      const size_t bytes = size_t(event->data_len);
      if (w->used + bytes >= kResponseBytes) w->overflow = true;
      else if (!w->overflow) { memcpy(w->response + w->used, event->data, bytes); w->used += bytes; }
    }
    return ESP_OK;
  }
  bool tlsRoom() {
    const auto ready = [&] {
      internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      largestInternalBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      return telegram::tlsHeapReady(internalFree.load(), largestInternalBlock.load(),
        worker->tlsConnected.load(), kTlsRecordBytes);
    };
    if (!ready()) {
      closeHttp();
      if (!ready()) { lastHttp = -2; ++memoryDeferrals; return false; }
    }
    return true;
  }
  bool request(const char *method, const String &body, JsonDocument &response) {
    auto &w = *worker;
    w.used = 0; w.overflow = false; w.serverRetry = 0;
    if (!tlsRoom()) return false;
    String url = String("https://api.telegram.org/bot") + token + "/" + method;
    if (!w.http) {
      esp_http_client_config_t config{};
      config.url = url.c_str(); config.timeout_ms = 5000;
      config.crt_bundle_attach = esp_crt_bundle_attach;
      config.disable_auto_redirect = true;
      config.event_handler = httpEvent; config.user_data = &w;
      config.keep_alive_enable = true; config.buffer_size = 1024; config.buffer_size_tx = 1024;
      w.http = esp_http_client_init(&config);
      if (!w.http) { lastHttp = -3; return false; }
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
    if (error) { lastHttp = -4; return false; }
    w.serverRetry = response["parameters"]["retry_after"] | 0U;
    return code == 200 && response["ok"].as<bool>();
  }
  void closeHttp() {
    if (worker->http) { esp_http_client_cleanup(worker->http); worker->http = nullptr; }
    worker->tlsConnected = false;
  }
  void backoff(telegram::RetryLane &lane) {
    ++failed;
    lane.fail(millis(), worker->serverRetry);
  }
  bool sendMessage(const Message &message, bool critical) {
    if (message.event && !telegram::allowEvent(mode(), critical, message.scope)) return true;
    JsonDocument body(&allocator), response(&allocator);
    body["chat_id"] = chatId; body["text"] = message.text;
    body["disable_notification"] = !critical;
    if (message.menu) {
      JsonObject markup = body["reply_markup"].to<JsonObject>();
      markup["resize_keyboard"] = true; markup["is_persistent"] = true; markup["one_time_keyboard"] = false;
      JsonArray rows = markup["keyboard"].to<JsonArray>();
      JsonArray first = rows.add<JsonArray>(); first.add(telegram::kEssentialButton); first.add(telegram::kBriefButton);
      JsonArray second = rows.add<JsonArray>(); second.add(telegram::kDetailedButton); second.add("/status");
      JsonArray third = rows.add<JsonArray>(); third.add("/plot"); third.add("/modalita");
    }
    String payload; serializeJson(body, payload);
    if (!request("sendMessage", payload, response)) { backoff(worker->messages); return false; }
    if (message.pin) worker->pinMessageId = response["result"]["message_id"] | int64_t(0);
    worker->messages.success(millis()); worker->lastSend = millis();
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
      backoff(worker->messages); return;
    }
    worker->pinMessageId = 0; worker->messages.success(millis());
  }
  void registerCommands() {
    JsonDocument body(&allocator), response(&allocator);
    body["scope"]["type"] = "chat"; body["scope"]["chat_id"] = chatId;
    const char *names[] = {"status", "plot", "modalita", "help", "start", "essenziale", "sintetica", "dettagliata"};
    const char *descriptions[] = {"Stato completo del forno", "Grafico attuale target e media PT100", "Scegli le notifiche", "Guida ai comandi", "Mostra e fissa la guida", "Allarmi e report", "Eventi brevi e report", "Tutti i dettagli e avvisi sonde"};
    auto commands = body["commands"].to<JsonArray>();
    for (unsigned i = 0; i < 8; ++i) { auto item = commands.add<JsonObject>(); item["command"] = names[i]; item["description"] = descriptions[i]; }
    String payload; serializeJson(body, payload);
    if (request("setMyCommands", payload, response)) { worker->commandsSet = true; worker->setup.success(millis()); }
    else backoff(worker->setup);
  }
  bool sendDocument() {
    auto &w = *worker;
    const bool plot = w.document->plot;
    w.serverRetry = 0;
    if (!w.pdf) {
      w.pdf = static_cast<char *>(ps_malloc(reportpdf::kCapacity));
      if (w.pdf && plot) {
        auto *raster = static_cast<uint8_t *>(ps_malloc(plotpng::kRasterBytes));
        if (raster) w.pdfBytes = plotpng::generate(w.pdf, reportpdf::kCapacity, raster, plotpng::kRasterBytes,
          w.document->points, w.document->count, w.document->report.startMs, w.document->report.endMs, renderYield);
        free(raster);
      } else if (w.pdf) w.pdfBytes = reportpdf::generate(w.pdf, reportpdf::kCapacity, w.document->report,
        w.document->points, w.document->count, w.document->partial, renderYield);
      if (!w.pdfBytes) {
        finishDocument(false);
        notify(plot ? "⚠️ Grafico non generato: memoria insufficiente. Riprova /plot." : "⚠️ PDF NON GENERATO\nMemoria report insufficiente. Il riepilogo resta disponibile dalla dashboard.");
        return false;
      }
    }
    // Release the polling TLS session before opening the streaming upload.
    closeHttp();
    if (!available || !tlsRoom()) { backoff(w.attachments); return false; }
    char name[128], stamp[64]; reportpdf::date(w.document->report.endUtc, stamp, sizeof(stamp));
    if (plot) snprintf(name, sizeof(name), "Plot_%u.png", unsigned(w.document->created));
    else reportpdf::filename(w.document->report, name, sizeof(name));
    const String caption = plot ? String("📈 TARGET / MEDIA PT100\nAggiornato: ") + stamp +
      "\nUltimi 30 minuti disponibili · " + String(w.document->count) + " campioni. Nuova immagine con /plot." :
      String("📄 REPORT DI FINE CICLO\n") + w.document->report.recipeName + "\n" + reportpdf::outcome(w.document->report.outcome);
    const String boundary = "OvenPdf" + String(esp_random(), HEX) + String(esp_random(), HEX);
    const String prefix = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
      String(static_cast<long long>(chatId)) + "\r\n--" + boundary +
      "\r\nContent-Disposition: form-data; name=\"disable_notification\"\r\n\r\ntrue\r\n--" + boundary +
      "\r\nContent-Disposition: form-data; name=\"caption\"\r\n\r\n" + caption + "\r\n--" + boundary +
      "\r\nContent-Disposition: form-data; name=\"" + (plot ? "photo" : "document") + "\"; filename=\"" + name +
      "\"\r\nContent-Type: " + (plot ? "image/png" : "application/pdf") + "\r\n\r\n";
    const String suffix = "\r\n--" + boundary + "--\r\n";
    const String url = String("https://api.telegram.org/bot") + token + (plot ? "/sendPhoto" : "/sendDocument");
    esp_http_client_config_t config{};
    config.url = url.c_str(); config.timeout_ms = 5000;
    config.crt_bundle_attach = esp_crt_bundle_attach; config.disable_auto_redirect = true;
    config.buffer_size = 1024; config.buffer_size_tx = 1024;
    auto client = esp_http_client_init(&config);
    if (!client) { lastHttp = -3; backoff(w.attachments); return false; }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", ("multipart/form-data; boundary=" + boundary).c_str());
    const uint32_t began = millis();
    bool interrupted = false, mayHaveDelivered = false;
    bool ok = esp_http_client_open(client, prefix.length() + w.pdfBytes + suffix.length()) == ESP_OK;
    const auto write = [&](const char *data, size_t bytes, bool finalBoundary = false) {
      size_t at = 0;
      while (at < bytes && available && uint32_t(millis() - began) < 25000) {
        if (uxQueueMessagesWaiting(alarms) || uxQueueMessagesWaiting(replies)) { interrupted = true; return false; }
        // A failed final write may still have reached the server. Do not repeat
        // an upload with an ambiguous result, even if its reply is lost.
        if (finalBoundary) mayHaveDelivered = true;
        const int n = esp_http_client_write(client, data + at, std::min(size_t(1024), bytes - at));
        if (n <= 0) return false;
        at += n; vTaskDelay(pdMS_TO_TICKS(1));
      }
      return at == bytes;
    };
    ok = ok && write(prefix.c_str(), prefix.length()) && write(w.pdf, w.pdfBytes) && write(suffix.c_str(), suffix.length(), true);
    if (ok) ok = esp_http_client_fetch_headers(client) >= 0;
    const int code = ok ? esp_http_client_get_status_code(client) : -1;
    w.response[0] = '\0';
    if (ok) telegram::readDocumentBody(w.response, kResponseBytes,
      [&](char *data, size_t bytes) { return esp_http_client_read(client, data, int(bytes)); },
      [&]() { return esp_http_client_is_complete_data_received(client); },
      [&]() { return available.load() && uint32_t(millis() - began) < 30000; });
    esp_http_client_cleanup(client); lastHttp = code; w.serverRetry = 0;
    // A new alarm preempts the attachment; preserve the PDF without imposing
    // the upload retry delay on that alarm's notification.
    if (interrupted && !mayHaveDelivered) { w.attachments.success(millis()); return false; }
    JsonDocument response(&allocator), filter(&allocator);
    filter["ok"] = true; filter["parameters"]["retry_after"] = true;
    filter["result"]["message_id"] = true;
    const bool validResponse = !deserializeJson(response, w.response, DeserializationOption::Filter(filter)) &&
      response["ok"].is<bool>();
    if (validResponse)
      w.serverRetry = response["parameters"]["retry_after"] | 0U;
    const auto result = telegram::documentResult(mayHaveDelivered, code, validResponse,
      response["ok"].as<bool>(), response["result"]["message_id"] | int64_t(0));
    if (result == telegram::DocumentResult::Retry) { backoff(w.attachments); return false; }
    if (result != telegram::DocumentResult::Confirmed) {
      ++failed;
      if (!plot && result == telegram::DocumentResult::Uncertain) ++reportsUnconfirmed;
      finishDocument(false); w.attachments.success(millis());
      // Silent, single warning; the attachment itself is no longer queued.
      notify(plot ? "⚠️ Invio grafico non confermato. Richiedi una nuova immagine con /plot." : result == telegram::DocumentResult::Uncertain ?
        "⚠️ Conferma PDF Telegram non ricevuta. Per evitare duplicati il report non verrà reinviato automaticamente; se manca in chat, scaricalo dalla dashboard." :
        "⚠️ PDF rifiutato da Telegram. Scarica il report dalla dashboard e verifica la configurazione del bot.");
      return false;
    }
    w.attachments.success(millis()); w.lastSend = millis();
    ++sent; lastSuccess = millis(); finishDocument(true); return true;
  }
  void poll() {
    auto &w = *worker;
    JsonDocument body(&allocator), response(&allocator);
    body["offset"] = w.offset; body["limit"] = 1; body["timeout"] = 2;
    body["allowed_updates"].to<JsonArray>().add("message");
    String payload; serializeJson(body, payload);
    if (!request("getUpdates", payload, response)) { backoff(w.polling); return; }
    ++pollCount; lastPoll = millis();
    bool found = false;
    for (JsonObjectConst update : response["result"].as<JsonArrayConst>()) {
      found = true;
      const int64_t id = update["update_id"] | int64_t(-1);
      if (id < 0 || id < w.offset) continue;
      w.offset = id + 1; // Also acknowledge foreign, expired and unsupported updates.
      const JsonObjectConst message = update["message"];
      if (!telegram::authorized(chatId, message["chat"]["id"] | int64_t(0),
          message["from"]["id"] | int64_t(0), message["chat"]["type"] | "")) continue;
      if (!telegram::fresh(message["date"] | int64_t(0), int64_t(time(nullptr)))) { ++staleCommands; continue; }
      const auto command = telegram::command(message["text"] | "");
      if (command != telegram::Command::None) ++commandsReceived;
      switch (command) {
        case telegram::Command::Status: statusRequest = true; break;
        case telegram::Command::Plot: plotRequest = true; break;
        case telegram::Command::Help:
          notify(String(telegram::kGuideText) + "\n\nModalità attiva: " + telegram::modeName(mode()),
            false, strcmp(message["text"] | "", "/start") == 0, true);
          break;
        case telegram::Command::ModeMenu:
          reply(String("🎛 Modalità attiva: ") + telegram::modeName(mode()) + "\n\n" + telegram::kModeMenuText, true);
          break;
        case telegram::Command::Essential: requestedMode = int(telegram::Mode::Essential); break;
        case telegram::Command::Brief: requestedMode = int(telegram::Mode::Brief); break;
        case telegram::Command::Detailed: requestedMode = int(telegram::Mode::Detailed); break;
        default: break;
      }
    }
    w.polling.success(millis(), found ? 200 : 2000);
  }
  void run() {
    auto &w = *worker;
    for (;;) {
      if (!available.load()) { closeHttp(); vTaskDelay(pdMS_TO_TICKS(500)); continue; }
      const uint32_t now = millis();
      if (!w.hasCritical) w.hasCritical = xQueueReceive(alarms, &w.critical, 0) == pdTRUE;
      if (!w.hasReply) w.hasReply = xQueueReceive(replies, &w.reply, 0) == pdTRUE;
      if (!w.hasNotice) w.hasNotice = xQueueReceive(notices, &w.notice, 0) == pdTRUE;
      if (!w.document) xQueueReceive(documents, &w.document, 0);
      if (w.document && uint32_t(now - w.document->created) > (w.document->plot ? 60000u : 86400000u)) {
        const bool plot = w.document->plot; finishDocument(false);
        if (plot) reply("📈 Richiesta grafico scaduta durante l'attesa. Riprova /plot.");
      }
      if (w.hasReply && telegram::expired(now, w.reply.created, true, false)) { w.hasReply = false; ++dropped; }
      if (w.hasNotice && telegram::expired(now, w.notice.created, false, false)) { w.hasNotice = false; ++dropped; }
      // A selection also suppresses old routine events still waiting in the queue.
      if (w.hasNotice && w.notice.event && !telegram::allowEvent(mode(), false, w.notice.scope)) w.hasNotice = false;
      Message *out = w.hasCritical ? &w.critical : w.hasReply ? &w.reply : w.hasNotice ? &w.notice : nullptr;
      if (out && w.messages.ready(now) && uint32_t(now - w.lastSend) >= 1100) {
        if (sendMessage(*out, out == &w.critical)) {
          if (out == &w.critical) w.hasCritical = false;
          else if (out == &w.reply) w.hasReply = false;
          else w.hasNotice = false;
        }
      }
      else if (!out && w.pinMessageId && w.messages.ready(now)) pinGuide();
      else if (!out && w.polling.ready(now)) poll();
      else if (!out && w.document && w.attachments.ready(now) && uint32_t(now - w.document->created) >= 2000 && uint32_t(now - w.lastSend) >= 1100)
        sendDocument();
      // Polling has its own retry deadline, independent of messages and PDFs.
      if (w.polling.ready(millis())) poll();
      if (!out && !w.commandsSet && w.setup.ready(millis())) registerCommands();
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
  telegram::Mode mode() const { return telegram::modeFrom(modeValue.load()); }
  void setMode(telegram::Mode selected) { modeValue = int(selected); }
  int takeModeRequest() { return requestedMode.exchange(-1); }
  bool takeStatusRequest() { return running && statusRequest.exchange(false); }
  bool takePlotRequest() { return running && plotRequest.exchange(false); }
  bool notify(const String &text, bool critical = false, bool pin = false, bool menu = false, bool event = false, telegram::EventScope scope = telegram::EventScope::Selected) {
    if (!running) return false;
    if (event && !telegram::allowEvent(mode(), critical, scope)) return true;
    Message message{}; copyText(message, text); message.pin = pin; message.menu = menu; message.event = event; message.scope = scope;
    const bool queued = xQueueSend(critical ? alarms : notices, &message, 0) == pdTRUE;
    if (!queued) ++dropped;
    return queued;
  }
  bool notifyEvent(const String &text, bool critical = false, telegram::EventScope scope = telegram::EventScope::Selected) { return notify(text, critical, false, false, true, scope); }
  template <typename Sample>
  bool report(const reports::Snapshot &snapshot, const Sample *curve, size_t count, bool partial) {
    if (!running) return false;
    if (queuedCycles.contains(snapshot.boot, snapshot.startMs, snapshot.endMs)) { ++duplicateReportsSkipped; return true; }
    if (reportsPending >= 4 || count > 4321 || (count && !curve)) { ++reportsDropped; return false; }
    auto *job = static_cast<ReportJob *>(ps_malloc(sizeof(ReportJob)));
    if (!job) { ++reportsDropped; return false; }
    new (job) ReportJob(); job->report = snapshot; job->created = millis(); job->partial = partial;
    job->points = count ? static_cast<reportpdf::Point *>(ps_malloc(count * sizeof(reportpdf::Point))) : nullptr;
    if (count && !job->points) { releaseReport(job); ++reportsDropped; return false; }
    job->count = count;
    for (size_t i = 0; i < count; ++i) job->points[i] = {curve[i].ms,
      (curve[i].flags & 3) == 3 ? (curve[i].t1 + curve[i].t2) * 0.5f : NAN,
      curve[i].flags & 8 ? curve[i].setpoint : NAN, curve[i].step, curve[i].flags};
    ++reportsPending;
    if (xQueueSend(documents, &job, 0) != pdTRUE) { --reportsPending; releaseReport(job); ++reportsDropped; return false; }
    queuedCycles.remember(snapshot.boot, snapshot.startMs, snapshot.endMs);
    return true;
  }
  template <typename Sample>
  bool plot(const Sample *curve, size_t capacity, size_t head, size_t count, const Sample &current) {
    if (!running || plotsPending || !curve || !capacity || count > capacity) return false;
    auto *job = static_cast<ReportJob *>(ps_malloc(sizeof(ReportJob)));
    if (!job) return false;
    new (job) ReportJob(); job->plot = true; job->created = millis();
    job->points = static_cast<reportpdf::Point *>(ps_malloc(257 * sizeof(reportpdf::Point)));
    if (!job->points) { releaseReport(job); return false; }
    job->count = plotpng::snapshot(curve, capacity, head, count, current, job->points, 257);
    if (!job->count) { releaseReport(job); return false; }
    job->report.startMs = job->points[0].ms; job->report.endMs = current.ms;
    const auto epoch = time(nullptr); job->report.endUtc = epoch >= 1700000000 ? uint64_t(epoch) : 0;
    ++plotsPending;
    if (xQueueSend(documents, &job, 0) != pdTRUE) { --plotsPending; releaseReport(job); ++plotsDropped; return false; }
    return true;
  }
  void reply(const String &text, bool menu = false) {
    if (!running) return;
    Message message{}; copyText(message, text); message.menu = menu; xQueueOverwrite(replies, &message);
  }
  void status(JsonObject object) const {
    object["configured"] = configured; object["running"] = running.load();
    object["notificationMode"] = telegram::modeKey(mode());
    object["networkReady"] = available.load(); object["sent"] = sent.load();
    object["failedAttempts"] = failed.load(); object["dropped"] = dropped.load();
    object["lastHttpCode"] = lastHttp.load(); object["lastSentAtMs"] = lastSuccess.load();
    object["plotsSent"] = plotsSent.load(); object["plotsPending"] = plotsPending.load(); object["plotsDropped"] = plotsDropped.load();
    object["reportsSent"] = reportsSent.load(); object["reportsPending"] = reportsPending.load();
    object["reportsDropped"] = reportsDropped.load();
    object["reportsUnconfirmed"] = reportsUnconfirmed.load();
    object["duplicateReportsSkipped"] = duplicateReportsSkipped.load();
    object["internalFreeBytes"] = internalFree.load(); object["largestInternalBlockBytes"] = largestInternalBlock.load();
    object["tlsConnected"] = worker && worker->tlsConnected.load();
    object["tlsRecordBytes"] = kTlsRecordBytes; object["memoryDeferrals"] = memoryDeferrals.load();
    object["pollCount"] = pollCount.load(); object["lastPollAtMs"] = lastPoll.load();
    object["commandsReceived"] = commandsReceived.load(); object["staleCommands"] = staleCommands.load();
  }
};
