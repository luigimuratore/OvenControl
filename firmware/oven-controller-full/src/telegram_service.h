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
#include "telegram_users.h"
#include "telegram_delivery.h"
#include "telegram_runtime.h"
#include "report_pdf.h"
#include "plot_png.h"
#include "plot_snapshot.h"
#include "telegram_outbox.h"

// Arduino's WiFiClientSecure library shadows the SDK's esp_crt_bundle.h.
// Use the native IDF bundle (built into this SDK), not its Arduino replacement.
extern "C" esp_err_t esp_crt_bundle_attach(void *conf);

// The worker owns all HTTPS/JSON state. The loop only copies fixed-size messages
// into PSRAM queues with zero wait. No sensor, PID, Preferences or GPIO access here.
class TelegramService {
  struct Message { uint32_t created; uint64_t createdUtc=0; int diskSlot=-1; int64_t chat=0; bool registration=false, pin=false, menu=false, plotMenu=false, event=false; telegram::EventScope scope=telegram::EventScope::Selected; char text[telegram::kMessageBytes]; };
  struct ReportJob {
    reports::Snapshot report;
    reportpdf::Point *points = nullptr;
    size_t count = 0;
    bool partial = false, plot = false;
    unsigned plotMinutes = 30;
    uint32_t created = 0;
    int64_t recipients[telegram::kMaxUsers]{};
    size_t recipientCount=0, recipientIndex=0;
    bool recipientFailed=false;
    int diskSlot = -1;
    uint64_t expiresUtc = 0;
  };
  struct StoredReport {
    reports::Snapshot report;
    uint32_t count = 0, recipientCount = 0, recipientIndex = 0;
    int64_t recipients[telegram::kMaxUsers]{};
    bool partial = false, recipientFailed = false;
  };
  struct Worker {
    telegram::DurableOutbox outbox;
    Message critical{}, notice{}, reply{};
    bool hasCritical = false, hasNotice = false, hasReply = false;
    char *response = nullptr;
    size_t used = 0;
    bool overflow = false;
    esp_http_client_handle_t http = nullptr;
    int64_t offset = -1;
    int64_t pinMessageId[telegram::kMaxUsers]{}, pinChat[telegram::kMaxUsers]{}, commandChats[telegram::kMaxUsers]{};
    uint32_t lastSend = 0;
    unsigned serverRetry = 0;
    std::atomic<bool> tlsConnected{false};
    telegram::RetryLane messages, polling, attachments, setup, storage;
    bool identityLoaded=false;
    ReportJob *document = nullptr;
    ReportJob *plotWaiting = nullptr;
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
  mutable portMUX_TYPE usersMux=portMUX_INITIALIZER_UNLOCKED;
  telegram::Users users;
  char username[33]{};
  Worker *worker = nullptr;
  QueueHandle_t alarms = nullptr, notices = nullptr, replies = nullptr;
  QueueHandle_t documents = nullptr, requests=nullptr;
  StaticQueue_t alarmControl{}, noticeControl{}, replyControl{};
  StaticQueue_t documentControl{};
  StaticQueue_t requestControl{};
  uint8_t requestStorage[16*sizeof(telegram::UserRequest)]{};
  uint8_t documentStorage[3 * sizeof(ReportJob *)]{};
  uint8_t *alarmStorage = nullptr, *noticeStorage = nullptr, *replyStorage = nullptr;
  std::atomic<bool> available{false}, running{false};
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
    worker->outbox.erase(worker->document->diskSlot);
    releaseReport(worker->document); worker->document = nullptr;
    free(worker->pdf); worker->pdf = nullptr; worker->pdfBytes = 0;
    if (plot) { --plotsPending; if (success) ++plotsSent; else ++plotsDropped; }
    else { --reportsPending; if (success) ++reportsSent; else ++reportsDropped; }
  }
  void finishRecipient(bool success) {
    auto *job=worker->document;
    job->recipientFailed |= !success;
    ++job->recipientIndex;
    worker->outbox.mark(job->diskSlot, job->recipientIndex, false, job->recipientFailed);
    if(job->recipientIndex>=job->recipientCount) finishDocument(!job->recipientFailed);
  }
  void stageMessages(QueueHandle_t queue, telegram::StoredKind kind) {
    if (!worker->outbox.active()) return;
    Message message{};
    // Peek leaves the original queued if flash is full/unavailable.
    for (unsigned n=0; n<80 && xQueuePeek(queue,&message,0)==pdTRUE; ++n) {
      const int slot=worker->outbox.put(kind,&message,sizeof(message));
      if(slot<0)break;
      xQueueReceive(queue,&message,0);
    }
  }
  int persistReport(const ReportJob &job) {
    StoredReport stored; stored.report=job.report; stored.count=job.count;
    stored.recipientCount=job.recipientCount; stored.partial=job.partial;
    stored.recipientIndex=job.recipientIndex;stored.recipientFailed=job.recipientFailed;
    memcpy(stored.recipients,job.recipients,sizeof(stored.recipients));
    return worker->outbox.put(telegram::StoredKind::Report,&stored,sizeof(stored),job.points,job.count*sizeof(reportpdf::Point));
  }
  void stageDocuments() {
    if (!worker->outbox.active()) return;
    auto *held=worker->document;
    if(held && !held->plot && held->diskSlot<0) {
      // A report dequeued during a storage failure can become durable later.
      const int slot=persistReport(*held);
      if(slot<0)return;
      held->diskSlot=slot;worker->outbox.loaded(slot);
    }
    ReportJob *job=nullptr;
    while (xQueuePeek(documents,&job,0)==pdTRUE) {
      if(job->plot) {
        if(worker->plotWaiting)break;
        xQueueReceive(documents,&worker->plotWaiting,0); continue;
      }
      const int slot=persistReport(*job);
      if(slot<0)break;
      xQueueReceive(documents,&job,0); releaseReport(job);
      // The logical pending count now belongs to the durable record.
    }
  }
  bool restoreMessage(Message &message, telegram::StoredKind kind) {
    auto &outbox=worker->outbox;
    for (int slot=outbox.next(kind); slot>=0; slot=outbox.next(kind)) {
      const auto header=outbox.entry(slot).header;
      if(!outbox.read(slot,&message,sizeof(message)) || message.registration || message.chat<=0 ||
          !memchr(message.text,0,sizeof(message.text)) || unsigned(message.scope)>unsigned(telegram::EventScope::DetailedOnly)) {
        outbox.erase(slot); ++dropped; continue;
      }
      const auto epoch=time(nullptr);
      const uint64_t createdUtc=message.createdUtc?message.createdUtc:header.utc;
      if(kind==telegram::StoredKind::Notice && createdUtc && epoch>=1700000000 &&
          uint64_t(epoch)>createdUtc+telegram::kNoticeLifetimeMs/1000) {
        outbox.erase(slot); ++dropped; continue;
      }
      const uint64_t age=createdUtc && epoch>=1700000000 && uint64_t(epoch)>=createdUtc ? uint64_t(epoch)-createdUtc : 0;
      message.created=millis()-uint32_t(std::min(age,uint64_t(86400))*1000); message.diskSlot=slot; outbox.loaded(slot); return true;
    }
    return false;
  }
  ReportJob *restoreDocument() {
    auto &outbox=worker->outbox;
    const int slot=outbox.next(telegram::StoredKind::Report);
    if(slot<0)return nullptr;
    const auto entry=outbox.entry(slot);
    if(entry.header.metadata!=sizeof(StoredReport) || entry.header.payload>4321*sizeof(reportpdf::Point) || entry.header.payload%sizeof(reportpdf::Point)) {
      outbox.erase(slot);--reportsPending;++reportsDropped;return nullptr;
    }
    auto *job=static_cast<ReportJob *>(ps_malloc(sizeof(ReportJob)));
    if(!job)return nullptr;
    new(job)ReportJob(); StoredReport stored;
    job->points=entry.header.payload?static_cast<reportpdf::Point *>(ps_malloc(entry.header.payload)):nullptr;
    if (entry.header.payload&&!job->points) {releaseReport(job);return nullptr;}
    if (!outbox.read(slot,&stored,sizeof(stored),job->points,entry.header.payload)) {
      releaseReport(job);outbox.erase(slot);--reportsPending;++reportsDropped;return nullptr;
    }
    bool valid=stored.count<=4321 && stored.count*sizeof(reportpdf::Point)==entry.header.payload &&
      stored.recipientCount>0 && stored.recipientCount<=telegram::kMaxUsers && entry.progress.index<=stored.recipientCount && stored.recipientIndex<=stored.recipientCount &&
      reports::supportedVersion(stored.report.version) && stored.report.stepCount<=reports::kMaxSteps &&
      memchr(stored.report.recipeName,0,sizeof(stored.report.recipeName)) && memchr(stored.report.reason,0,sizeof(stored.report.reason));
    for(size_t i=0;i<stored.recipientCount && i<telegram::kMaxUsers;++i)valid &= stored.recipients[i]>0;
    if(!valid){releaseReport(job);outbox.erase(slot);--reportsPending;++reportsDropped;return nullptr;}
    job->report=stored.report; job->count=stored.count; job->partial=stored.partial;
    job->recipientCount=stored.recipientCount; memcpy(job->recipients,stored.recipients,sizeof(job->recipients));
    job->recipientIndex=std::max(stored.recipientIndex,entry.progress.index); job->diskSlot=slot; job->created=millis();
    job->recipientFailed=stored.recipientFailed||entry.progress.failed;
    const uint64_t createdUtc=stored.report.endUtc?stored.report.endUtc:entry.header.utc;
    job->expiresUtc=createdUtc?createdUtc+86400:0;
    if(entry.progress.ambiguous && job->recipientIndex<job->recipientCount) {
      // A power cut after the durable send-intent may have delivered this
      // recipient's PDF. Never replay an ambiguous upload; continue the others.
      reply("⚠️ Invio PDF precedente al riavvio non confermato. Per evitare duplicati non viene ripetuto; controlla la chat o scaricalo dalla dashboard.",false,false,job->recipients[job->recipientIndex]);
      ++job->recipientIndex; job->recipientFailed=true; ++reportsUnconfirmed;
      outbox.mark(slot,job->recipientIndex,false,true);
    }
    outbox.loaded(slot);
    if(job->recipientIndex>=job->recipientCount) {
      outbox.erase(slot);releaseReport(job);--reportsPending;return nullptr;
    }
    return job;
  }
  int pendingPin() const {
    for(size_t i=0;i<telegram::kMaxUsers;++i)if(worker->pinMessageId[i])return int(i);
    return -1;
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
    filter["result"][0]["message"]["from"]["first_name"] = true;
    filter["result"][0]["message"]["from"]["username"] = true;
    filter["result"][0]["message"]["from"]["is_bot"] = true;
    // sendMessage's result is an object; retain only its ID for guide pinning.
    if (strcmp(method, "sendMessage") == 0) {
      filter.remove("result"); filter["result"]["message_id"] = true;
    }
    if (!strcmp(method,"getMe")) {filter.remove("result");filter["result"]["username"]=true;}
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
    if (!message.registration && !authorizedChat(message.chat)) return true;
    if (message.event && !telegram::allowEvent(mode(message.chat), critical, message.scope)) return true;
    JsonDocument body(&allocator), response(&allocator);
    body["chat_id"] = message.chat; body["text"] = message.text;
    body["disable_notification"] = !critical;
    const auto keyboard = telegram::keyboardLayout(telegram::messageKeyboard(message.menu, message.plotMenu));
    if (keyboard.rows) {
      JsonObject markup = body["reply_markup"].to<JsonObject>();
      markup["resize_keyboard"] = true; markup["is_persistent"] = true; markup["one_time_keyboard"] = false;
      JsonArray rows = markup["keyboard"].to<JsonArray>();
      for (size_t i = 0; i < keyboard.rows; ++i) {
        JsonArray row = rows.add<JsonArray>();
        for (const auto *button : keyboard.buttons[i]) if (button) row.add(button);
      }
    }
    String payload; serializeJson(body, payload);
    if (!request("sendMessage", payload, response)) {
      // A recipient who blocks the bot must not prevent delivery to everyone else.
      if(lastHttp==400||lastHttp==403){++dropped;worker->messages.success(millis());return true;}
      backoff(worker->messages); return false;
    }
    if (message.pin) {
      for(size_t i=0;i<telegram::kMaxUsers;++i)if(!worker->pinMessageId[i]||worker->pinChat[i]==message.chat){
        worker->pinMessageId[i]=response["result"]["message_id"]|int64_t(0);worker->pinChat[i]=message.chat;break;
      }
    }
    worker->messages.success(millis()); worker->lastSend = millis();
    ++sent; lastSuccess = millis(); return true;
  }
  void pinGuide() {
    const int index=pendingPin();if(index<0)return;
    if(!authorizedChat(worker->pinChat[index])){worker->pinMessageId[index]=0;return;}
    JsonDocument body(&allocator), response(&allocator);
    body["chat_id"] = worker->pinChat[index]; body["message_id"] = worker->pinMessageId[index];
    body["disable_notification"] = true;
    String payload; serializeJson(body, payload);
    if (!request("pinChatMessage", payload, response)) {
      // A deleted message or inaccessible chat cannot be repaired by retrying.
      if (lastHttp == 400 || lastHttp == 403) { worker->pinMessageId[index] = 0; ++dropped; }
      backoff(worker->messages); return;
    }
    worker->pinMessageId[index] = 0; worker->messages.success(millis());
  }
  void registerCommands() {
    if(!worker->identityLoaded){
      JsonDocument response(&allocator);
      if(request("getMe","{}",response)){
        const char *name=response["result"]["username"]|"";
        const size_t length=strlen(name);
        bool valid=length>0&&length<sizeof(username);
        for(size_t i=0;i<length;++i)valid &= isalnum(static_cast<unsigned char>(name[i]))||name[i]=='_';
        if(valid){portENTER_CRITICAL(&usersMux);snprintf(username,sizeof(username),"%s",name);portEXIT_CRITICAL(&usersMux);worker->identityLoaded=true;}
        worker->setup.success(millis(),1000);
      }else backoff(worker->setup);
      return;
    }
    const auto current=userSettings();int slot=-1;int64_t recipient=0;
    for(size_t i=0;i<telegram::kMaxUsers;++i)if(!authorizedChat(worker->commandChats[i]))worker->commandChats[i]=0;
    for(const auto &u:current.entries)if(u.chat>0){
      bool known=false;for(auto id:worker->commandChats)known |= id==u.chat;
      if(!known){recipient=u.chat;break;}
    }
    if(!recipient){worker->setup.success(millis(),30000);return;}
    for(size_t i=0;i<telegram::kMaxUsers;++i)if(!worker->commandChats[i]){slot=int(i);break;}
    if(slot<0)return;
    JsonDocument body(&allocator), response(&allocator);
    body["scope"]["type"] = "chat"; body["scope"]["chat_id"] = recipient;
    auto commands = body["commands"].to<JsonArray>();
    for (const auto &entry : telegram::kBotCommands) {
      auto item = commands.add<JsonObject>(); item["command"] = entry.name; item["description"] = entry.description;
    }
    String payload; serializeJson(body, payload);
    if (request("setMyCommands", payload, response)) { worker->commandChats[slot]=recipient; worker->setup.success(millis(),1000); }
    else if(lastHttp==400||lastHttp==403){worker->commandChats[slot]=recipient;worker->setup.success(millis(),1000);}
    else backoff(worker->setup);
  }
  bool sendDocument() {
    auto &w = *worker;
    const bool plot = w.document->plot;
    const int64_t recipient=w.document->recipients[w.document->recipientIndex];
    if(!authorizedChat(recipient)){finishRecipient(false);return false;}
    w.serverRetry = 0;
    if (!w.pdf) {
      const size_t capacity = plot ? plotpng::kCapacity : reportpdf::kCapacity;
      w.pdf = static_cast<char *>(ps_malloc(capacity));
      if (w.pdf && plot) {
        auto *raster = static_cast<uint8_t *>(ps_malloc(plotpng::kRasterBytes));
        if (raster) w.pdfBytes = plotpng::generate(w.pdf, capacity, raster, plotpng::kRasterBytes,
          w.document->points, w.document->count, w.document->report.startMs, w.document->report.endMs, renderYield);
        free(raster);
      } else if (w.pdf) w.pdfBytes = reportpdf::generate(w.pdf, reportpdf::kCapacity, w.document->report,
        w.document->points, w.document->count, w.document->partial, renderYield);
      if (!w.pdfBytes) {
        finishDocument(false);
        reply(plot ? "⚠️ Grafico non generato: memoria insufficiente. Riprova /plot." : "⚠️ PDF NON GENERATO\nMemoria report insufficiente. Il riepilogo resta disponibile dalla dashboard.",false,false,recipient);
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
      "\nUltimi " + String(w.document->plotMinutes) + " minuti · storico disponibile, " + String(w.document->count) + " campioni. Nuova immagine dal menu /plot." :
      String("📄 REPORT DI FINE CICLO\n") + w.document->report.recipeName + "\n" + reportpdf::outcome(w.document->report.outcome);
    const String boundary = "OvenPdf" + String(esp_random(), HEX) + String(esp_random(), HEX);
    const String prefix = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
      String(static_cast<long long>(recipient)) + "\r\n--" + boundary +
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
        if (finalBoundary) {
          // Persist send-intent BEFORE the only write that can complete an
          // upload. A reset afterwards skips this recipient rather than spamming.
          if(!w.outbox.mark(w.document->diskSlot,w.document->recipientIndex,true,w.document->recipientFailed))return false;
          mayHaveDelivered = true;
        }
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
    if (result == telegram::DocumentResult::Retry) {
      // Telegram's explicit 429 proves rejection; a reboot may safely retry it.
      if(code==429)w.outbox.mark(w.document->diskSlot,w.document->recipientIndex,false,w.document->recipientFailed);
      backoff(w.attachments); return false;
    }
    if (result != telegram::DocumentResult::Confirmed) {
      ++failed;
      if (!plot && result == telegram::DocumentResult::Uncertain) ++reportsUnconfirmed;
      finishRecipient(false); w.attachments.success(millis());
      // Silent, single warning; the attachment itself is no longer queued.
      reply(plot ? "⚠️ Invio grafico non confermato. Richiedi una nuova immagine con /plot." : result == telegram::DocumentResult::Uncertain ?
        "⚠️ Conferma PDF Telegram non ricevuta. Per evitare duplicati il report non verrà reinviato automaticamente; se manca in chat, scaricalo dalla dashboard." :
        "⚠️ PDF rifiutato da Telegram. Scarica il report dalla dashboard e verifica la configurazione del bot.",false,false,recipient);
      return false;
    }
    w.attachments.success(millis()); w.lastSend = millis();
    ++sent; lastSuccess = millis(); finishRecipient(true); return true;
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
      const int64_t chat=message["chat"]["id"]|int64_t(0);
      if (!telegram::privateSender(chat,message["from"]["id"]|int64_t(0),message["chat"]["type"]|"") ||
          (message["from"]["is_bot"]|false)) continue;
      if (!telegram::fresh(message["date"]|int64_t(0),int64_t(time(nullptr)))){++staleCommands;continue;}
      const char *text=message["text"]|"";
      const bool allowed=authorizedChat(chat);
      if(!allowed && strcmp(text,"/start"))continue;
      telegram::UserRequest incoming;
      incoming.user.chat=chat;incoming.created=millis();
      telegram::copyUserText(incoming.user.name,sizeof(incoming.user.name),message["from"]["first_name"]|"Utente Telegram");
      telegram::copyUserText(incoming.user.username,sizeof(incoming.user.username),message["from"]["username"]|"");
      incoming.command=allowed?telegram::command(text):telegram::Command::None;
      if(!allowed || incoming.command==telegram::Command::Status || incoming.command==telegram::Command::Plot15 ||
          incoming.command==telegram::Command::Plot30 || incoming.command==telegram::Command::Plot60 ||
          incoming.command==telegram::Command::Essential || incoming.command==telegram::Command::Brief || incoming.command==telegram::Command::Detailed){
        if(xQueueSend(requests,&incoming,0)!=pdTRUE)++dropped;
      }else switch(incoming.command){
        case telegram::Command::Plot:reply(telegram::kPlotMenuText,false,true,chat);break;
        case telegram::Command::Help:
          notify(String(telegram::kGuideText)+"\n\nModalità attiva: "+telegram::modeName(mode(chat)),false,
            !strcmp(text,"/start"),true,false,telegram::EventScope::Selected,chat);break;
        case telegram::Command::ModeMenu:
          reply(String("🎛 Modalità attiva: ")+telegram::modeName(mode(chat))+"\n\n"+telegram::kModeMenuText,true,true,chat);break;
        case telegram::Command::MainMenu:
          reply(telegram::kMainMenuText,true,false,chat);break;
        default:break;
      }
      if(incoming.command!=telegram::Command::None)++commandsReceived;
    }
    w.polling.success(millis(), found ? 200 : 2000);
  }
  void run() {
    auto &w = *worker;
    w.outbox.begin(chatId);
    for(int i=0;i<telegram::DurableOutbox::kSlots;++i)
      if(w.outbox.entry(i).occupied && w.outbox.entry(i).progress.index!=255 && w.outbox.entry(i).header.kind==telegram::StoredKind::Report)++reportsPending;
    for (;;) {
      // Stage even while offline. All flash I/O is on this worker, never the
      // control task. Full/unavailable storage retains the original RAM queues.
      if(w.storage.ready(millis())) {
        const uint32_t errors=w.outbox.failures();
        stageMessages(alarms,telegram::StoredKind::Alarm);
        stageMessages(notices,telegram::StoredKind::Notice);
        stageDocuments();
        if(w.outbox.failures()!=errors)w.storage.fail(millis());
        else w.storage.success(millis(),200);
      }
      if (!available.load()) { closeHttp(); vTaskDelay(pdMS_TO_TICKS(100)); continue; }
      const uint32_t now = millis();
      if (!w.hasCritical) w.hasCritical = restoreMessage(w.critical,telegram::StoredKind::Alarm) || xQueueReceive(alarms, &w.critical, 0) == pdTRUE;
      if (!w.hasReply) w.hasReply = xQueueReceive(replies, &w.reply, 0) == pdTRUE;
      if (!w.hasNotice) w.hasNotice = restoreMessage(w.notice,telegram::StoredKind::Notice) || xQueueReceive(notices, &w.notice, 0) == pdTRUE;
      if (!w.document) {
        if(w.plotWaiting){w.document=w.plotWaiting;w.plotWaiting=nullptr;}
        else w.document=restoreDocument();
        if(!w.document)xQueueReceive(documents,&w.document,0);
      }
      if (w.document && ((w.document->expiresUtc && uint64_t(time(nullptr))>w.document->expiresUtc) ||
          uint32_t(now - w.document->created) > (w.document->plot ? 60000u : 86400000u))) {
        const bool plot = w.document->plot; const int64_t recipient=w.document->recipients[w.document->recipientIndex];finishDocument(false);
        if (plot) reply("📈 Richiesta grafico scaduta durante l'attesa. Riprova /plot.",false,false,recipient);
      }
      if (w.hasReply && telegram::expired(now, w.reply.created, true, false)) { w.hasReply = false; ++dropped; }
      if (w.hasNotice && telegram::expired(now, w.notice.created, false, false)) { w.outbox.erase(w.notice.diskSlot); w.hasNotice = false; ++dropped; }
      // A selection also suppresses old routine events still waiting in the queue.
      if (w.hasNotice && w.notice.event && !telegram::allowEvent(mode(w.notice.chat), false, w.notice.scope)) {w.outbox.erase(w.notice.diskSlot);w.hasNotice = false;}
      Message *out = w.hasCritical ? &w.critical : w.hasReply ? &w.reply : w.hasNotice ? &w.notice : nullptr;
      if (out && w.messages.ready(now) && uint32_t(now - w.lastSend) >= 1100) {
        if (sendMessage(*out, out == &w.critical)) {
          w.outbox.erase(out->diskSlot);
          if (out == &w.critical) w.hasCritical = false;
          else if (out == &w.reply) w.hasReply = false;
          else w.hasNotice = false;
        }
      }
      else if (!out && pendingPin()>=0 && w.messages.ready(now)) pinGuide();
      else if (!out && w.polling.ready(now)) poll();
      else if (!out && w.document && w.attachments.ready(now) && uint32_t(now - w.document->created) >= 2000 && uint32_t(now - w.lastSend) >= 1100)
        sendDocument();
      // Polling has its own retry deadline, independent of messages and PDFs.
      if (w.polling.ready(millis())) poll();
      if (!out && w.setup.ready(millis())) registerCommands();
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
  static void task(void *self) { static_cast<TelegramService *>(self)->run(); }
  static void copyText(Message &message, const String &text) {
    message.created = millis();
    const auto epoch=time(nullptr);message.createdUtc=epoch>=1700000000?uint64_t(epoch):0;
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
    alarmStorage = static_cast<uint8_t *>(ps_malloc(32 * sizeof(Message)));
    noticeStorage = static_cast<uint8_t *>(ps_malloc(48 * sizeof(Message)));
    replyStorage = static_cast<uint8_t *>(ps_malloc(8 * sizeof(Message)));
    if (worker) { new (worker) Worker(); worker->response = static_cast<char *>(ps_malloc(kResponseBytes)); }
    if (!worker || !worker->response || !alarmStorage || !noticeStorage || !replyStorage) {
      if (worker) { free(worker->response); free(worker); worker = nullptr; }
      free(alarmStorage); free(noticeStorage); free(replyStorage); return false;
    }
    alarms = xQueueCreateStatic(32, sizeof(Message), alarmStorage, &alarmControl);
    notices = xQueueCreateStatic(48, sizeof(Message), noticeStorage, &noticeControl);
    replies = xQueueCreateStatic(8, sizeof(Message), replyStorage, &replyControl);
    documents = xQueueCreateStatic(3, sizeof(ReportJob *), documentStorage, &documentControl);
    requests=xQueueCreateStatic(16,sizeof(telegram::UserRequest),requestStorage,&requestControl);
    // Wi-Fi is on core 0; the Arduino loop remains on core 1. Guard priority is 2.
    running = xTaskCreatePinnedToCore(task, "telegram", 12288, this, 1, nullptr, 0) == pdPASS;
    if (!running) {
      vQueueDelete(alarms); vQueueDelete(notices); vQueueDelete(replies); vQueueDelete(documents); vQueueDelete(requests);
      free(worker->response); free(worker); worker = nullptr;
      free(alarmStorage); free(noticeStorage); free(replyStorage);
    }
    return running;
  }
  bool statusRunning()const{return running.load();}
  void setAvailable(bool ready) { available = ready; }
  telegram::Users userSettings() const {portENTER_CRITICAL(&usersMux);auto copy=users;portEXIT_CRITICAL(&usersMux);return copy;}
  void setUsers(const telegram::Users &settings){portENTER_CRITICAL(&usersMux);users=settings;portEXIT_CRITICAL(&usersMux);}
  bool authorizedChat(int64_t chat)const{portENTER_CRITICAL(&usersMux);const bool ok=users.find(chat)>=0;portEXIT_CRITICAL(&usersMux);return ok;}
  telegram::Mode mode(int64_t chat=0) const {
    portENTER_CRITICAL(&usersMux);const int i=users.find(chat?chat:chatId);
    const auto selected=i<0?telegram::Mode::Brief:telegram::modeFrom(users.entries[i].mode);
    portEXIT_CRITICAL(&usersMux);return selected;
  }
  String botUsername()const{char copy[33];portENTER_CRITICAL(&usersMux);memcpy(copy,username,sizeof(copy));portEXIT_CRITICAL(&usersMux);return String(copy);}
  bool takeRequest(telegram::UserRequest &request){return running&&xQueueReceive(requests,&request,0)==pdTRUE;}
  bool notify(const String &text,bool critical=false,bool pin=false,bool menu=false,bool event=false,
              telegram::EventScope scope=telegram::EventScope::Selected,int64_t chat=0){
    if(!running)return false;
    if(!chat){
      bool ok=true;const auto current=userSettings();
      for(const auto &u:current.entries)if(u.chat>0)ok=notify(text,critical,pin,menu,event,scope,u.chat)&&ok;
      return ok;
    }
    if(!authorizedChat(chat))return false;
    if(event&&!telegram::allowEvent(mode(chat),critical,scope))return true;
    Message message{};copyText(message,text);message.chat=chat;message.pin=pin;message.menu=menu;message.event=event;message.scope=scope;
    const bool queued=xQueueSend(critical?alarms:notices,&message,0)==pdTRUE;if(!queued)++dropped;return queued;
  }
  bool notifyEvent(const String &text,bool critical=false,telegram::EventScope scope=telegram::EventScope::Selected,int64_t chat=0){return notify(text,critical,false,false,true,scope,chat);}
  template <typename Sample>
  bool report(const reports::Snapshot &snapshot, const Sample *curve, size_t count, bool partial) {
    if (!running) return false;
    if (queuedCycles.contains(snapshot.boot, snapshot.startMs, snapshot.endMs)) { ++duplicateReportsSkipped; return true; }
    if (reportsPending >= 4 || count > 4321 || (count && !curve)) { ++reportsDropped; return false; }
    auto *job = static_cast<ReportJob *>(ps_malloc(sizeof(ReportJob)));
    if (!job) { ++reportsDropped; return false; }
    new (job) ReportJob(); job->report = snapshot; job->created = millis(); job->partial = partial;
    const auto current=userSettings();for(const auto &u:current.entries)if(u.chat>0)job->recipients[job->recipientCount++]=u.chat;
    if(!job->recipientCount){releaseReport(job);return false;}
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
  bool plot(const Sample *curve, size_t capacity, size_t head, size_t count, const Sample &current, unsigned minutes, int64_t recipient) {
    if (!running || plotsPending || !authorizedChat(recipient) || !curve || !capacity || count > capacity || count > 4320 || !telegram::validPlotMinutes(minutes)) return false;
    auto *job = static_cast<ReportJob *>(ps_malloc(sizeof(ReportJob)));
    if (!job) return false;
    new (job) ReportJob(); job->plot = true; job->plotMinutes = minutes; job->created = millis();
    job->recipients[0]=recipient;job->recipientCount=1;
    // Keep the entire selected interval, including gaps and event markers.
    const size_t pointCapacity = std::max(size_t(2),count + 1);
    job->points = static_cast<reportpdf::Point *>(ps_malloc(pointCapacity * sizeof(reportpdf::Point)));
    if (!job->points) { releaseReport(job); return false; }
    job->count = plotpng::snapshot(curve, capacity, head, count, current, job->points, pointCapacity, minutes * 60000UL);
    if (!job->count) { releaseReport(job); return false; }
    job->report.startMs = job->points[0].ms; job->report.endMs = current.ms;
    const auto epoch = time(nullptr); job->report.endUtc = epoch >= 1700000000 ? uint64_t(epoch) : 0;
    ++plotsPending;
    if (xQueueSend(documents, &job, 0) != pdTRUE) { --plotsPending; releaseReport(job); ++plotsDropped; return false; }
    return true;
  }
  void reply(const String &text,bool menu=false,bool plotMenu=false,int64_t chat=0,bool registration=false){
    if(!running)return;
    Message message{};copyText(message,text);message.chat=chat?chat:chatId;message.menu=menu;message.plotMenu=plotMenu;message.registration=registration;
    if(xQueueSend(replies,&message,0)!=pdTRUE)++dropped;
  }
  void status(JsonObject object) const {
    object["configured"] = configured; object["running"] = running.load();
    object["notificationMode"] = telegram::modeKey(mode());
    object["userCount"]=userSettings().count();
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
    object["outboxReady"] = worker && worker->outbox.active();
    object["outboxRecords"] = worker ? worker->outbox.count() : 0;
    object["outboxErrors"] = worker ? worker->outbox.failures() : 0;
  }
};
