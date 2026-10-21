#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include <functional>
#include <new>
#include <algorithm>
#include <lwip/sockets.h>
#include <cerrno>
#include "http_request.h"


// Network I/O belongs exclusively to a worker. Complete requests and responses
// change ownership with acquire/release; callbacks and all oven state remain on
// the Arduino loop. Never put a mutex around network waits or PID sampling.
class ControlWebServer {
  enum class State : uint8_t { Empty, Reading, Ready, Handling, Responding };
  struct Exchange {
    ovenhttp::Request request;
    std::atomic<State> state{State::Empty};
    WiFiClient client;
    uint32_t started = 0, readyAt = 0, sequence = 0;
    int code = 200;
    String headers, type;
    const char *data = nullptr;
    char *owned = nullptr;
    size_t size = 0;
    bool headerReady = false;
    size_t sentBytes = 0;
    uint32_t sendAt = 0;
  };
  struct Route { const char *path = nullptr; ovenhttp::Method method = ovenhttp::Method::Get; std::function<void()> handler; bool safety = false; };
  static constexpr size_t kClients = 3, kRoutes = 32;
  size_t clientCapacity = kClients;
  Route routes[kRoutes]; size_t routeCount = 0;
  std::function<void()> missing;
  WiFiServer listener;
  Exchange *exchanges = nullptr, *current = nullptr;
  std::atomic<bool> running{false};
  std::atomic<uint32_t> served{0}, rejected{0};

  static const char *reason(int code) {
    switch (code) {
      case 200: return "OK"; case 204: return "No Content"; case 400: return "Bad Request";
      case 404: return "Not Found"; case 405: return "Method Not Allowed"; case 408: return "Request Timeout";
      case 409: return "Conflict"; case 413: return "Content Too Large"; case 431: return "Headers Too Large";
      case 503: return "Service Unavailable"; case 507: return "Insufficient Storage"; default: return "Error";
    }
  }
  static void release(Exchange &e) {
    e.client.stop(); free(e.owned); e.owned = nullptr; e.data = nullptr; e.size = 0;
    e.headers = ""; e.type = ""; e.request.reset();
    e.headerReady = false; e.sentBytes = 0;
    e.state.store(State::Empty, std::memory_order_release);
  }
  void respond(Exchange &e) {
    if (!e.headerReady) {
      char first[160]; snprintf(first, sizeof(first), "HTTP/1.1 %d %s\r\nContent-Length: %u\r\nConnection: close\r\n", e.code, reason(e.code), unsigned(e.size));
      e.headers = String(first) + (e.type.length() ? "Content-Type: " + e.type + "\r\n" : String()) + e.headers + "\r\n";
      e.headerReady = true; e.sendAt = millis();
    }
    const size_t header = e.headers.length(), total = header + (e.request.method == ovenhttp::Method::Head ? 0 : e.size);
    if (!e.client.connected() || uint32_t(millis() - e.sendAt) >= 5000) { ++rejected; release(e); return; }
    const bool inHeader = e.sentBytes < header;
    const char *data = inHeader ? e.headers.c_str() + e.sentBytes : e.data + e.sentBytes - header;
    const size_t remaining = inHeader ? header - e.sentBytes : total - e.sentBytes;
    // WiFiClient::write retries select() internally. Use a nonblocking socket
    // write so a slow download cannot monopolize this worker's other clients.
    const int n = ::send(e.client.fd(), data, std::min(size_t(1024), remaining), MSG_DONTWAIT);
    if (n > 0) e.sentBytes += size_t(n);
    else if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) { ++rejected; release(e); return; }
    if (e.sentBytes == total) { ++served; release(e); }
  }
  void run() {
    uint32_t sequence = 0;
    for (;;) {
      for (size_t i = 0; i < clientCapacity; ++i) {
        auto &e = exchanges[i]; const auto state = e.state.load(std::memory_order_acquire);
        if (state == State::Empty) {
          auto client = listener.available();
          if (client) { e.client = client; e.client.setTimeout(1); e.client.setNoDelay(true); e.started = millis(); e.sequence = ++sequence; e.state = State::Reading; }
        } else if (state == State::Reading) {
          char fragment[1024]; const int available = e.client.available();
          if (available > 0) {
            const int n = e.client.read(reinterpret_cast<uint8_t *>(fragment), std::min(size_t(available), sizeof(fragment)));
            if (n > 0) e.request.feed(fragment, size_t(n));
          }
          if (e.request.error || (!e.request.complete && uint32_t(millis() - e.started) >= 5000)) {
            e.code = e.request.error ? e.request.error : 408; ++rejected; e.state = State::Responding;
          } else if (e.request.complete) {
            e.readyAt = millis(); e.state.store(State::Ready, std::memory_order_release);
          } else if (!e.client.connected()) release(e);
        } else if (state == State::Responding) respond(e);
      }
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }
  static void task(void *self) { static_cast<ControlWebServer *>(self)->run(); }
  static String decode(const String &text) {
    String out; out.reserve(text.length());
    for (size_t i = 0; i < text.length(); ++i) {
      if (text[i] == '+') out += ' ';
      else if (text[i] == '%' && i + 2 < text.length() && isxdigit(text[i+1]) && isxdigit(text[i+2])) {
        char hex[3] = {text[i+1], text[i+2], 0}; out += char(strtoul(hex, nullptr, 16)); i += 2;
      } else out += text[i];
    }
    return out;
  }
 public:
  explicit ControlWebServer(uint16_t port) : listener(port, kClients) {}
  void on(const char *path, ovenhttp::Method method, std::function<void()> handler, bool safety = false) {
    if (routeCount < kRoutes) { auto &r = routes[routeCount++]; r.path=path; r.method=method; r.handler=handler; r.safety=safety; }
  }
  void onNotFound(std::function<void()> handler) { missing = handler; }
  bool begin() {
    exchanges = static_cast<Exchange *>(ps_calloc(kClients, sizeof(Exchange)));
    if (!exchanges) {
      clientCapacity = 1;
      exchanges = static_cast<Exchange *>(calloc(clientCapacity, sizeof(Exchange)));
    }
    if (!exchanges) return false;
    for (size_t i = 0; i < clientCapacity; ++i) new (&exchanges[i]) Exchange();
    listener.begin();
    running = bool(listener) && xTaskCreatePinnedToCore(task, "dashboard-http", 4096, this, 1, nullptr, 0) == pdPASS;
    if (!running) { listener.end(); for (size_t i=0;i<clientCapacity;++i) exchanges[i].~Exchange(); free(exchanges); exchanges=nullptr; }
    return running;
  }
  bool active() const { return running.load(); }
  size_t capacity() const { return clientCapacity; }
  uint32_t requestCount() const { return served.load(); }
  uint32_t rejectedCount() const { return rejected.load(); }
  bool expired() const { return current && uint32_t(millis()-current->readyAt)>1500; }
  bool expiredSafetyAction(const char *action) const { return current && ovenhttp::safetyAction(current->request.uri,action); }
  void handleClient() {
    if (!running) return;
    int selected = -1;
    for (size_t i = 0; i < clientCapacity; ++i) {
      auto &e = exchanges[i]; if (e.state.load(std::memory_order_acquire) != State::Ready) continue;
      if (selected < 0 ||
          (e.request.method == ovenhttp::Method::Post && exchanges[selected].request.method != ovenhttp::Method::Post) ||
          (e.request.method == exchanges[selected].request.method && int32_t(e.sequence-exchanges[selected].sequence)<0)) selected = int(i);
    }
    if (selected >= 0) {
      auto &e = exchanges[selected];
      // Drop commands whose browser timeout may already have elapsed.
      current = &e; e.state = State::Handling;
      {
        const char *query = strchr(e.request.uri, '?');
        const size_t pathSize = query ? size_t(query - e.request.uri) : strlen(e.request.uri);
        bool matched = false;
        for (size_t r = 0; r < routeCount; ++r) if ((routes[r].method == e.request.method ||
            (e.request.method == ovenhttp::Method::Head && routes[r].method == ovenhttp::Method::Get)) &&
            strlen(routes[r].path) == pathSize && !memcmp(routes[r].path, e.request.uri, pathSize)) {
          matched = true;
          if(expired() && !routes[r].safety)send(408);
          else routes[r].handler();
          break;
        }
        if (!matched) { if (missing) missing(); else send(404); }
      }
      current = nullptr; e.state.store(State::Responding, std::memory_order_release);
      return;
    }
  }
  bool hasArg(const char *name) const {
    if (!current || !name) return false;
    if (!strcmp(name, "plain")) return current->request.bodyBytes > 0;
    const char *query = strchr(current->request.uri, '?'); if (!query) return false;
    String q(query + 1); int begin = 0;
    while (begin < int(q.length())) {
      int end = q.indexOf('&', begin); if (end < 0) end = q.length();
      int eq = q.indexOf('=', begin); if (eq < begin || eq >= end) eq = end;
      if (decode(q.substring(begin, eq)) == name) return true;
      begin = end + 1;
    }
    return false;
  }
  String arg(const char *name) const {
    if (!current || !name) return "";
    if (!strcmp(name, "plain")) return String(current->request.body(), current->request.bodyBytes);
    const char *query = strchr(current->request.uri, '?');
    if (!query) return "";
    String q(query + 1); int begin = 0;
    while (begin < int(q.length())) {
      int end = q.indexOf('&', begin); if (end < 0) end = q.length();
      int eq = q.indexOf('=', begin);
      if (eq >= begin && eq < end && decode(q.substring(begin, eq)) == name) return decode(q.substring(eq + 1, end));
      begin = end + 1;
    }
    return "";
  }
  void sendHeader(const char *name, const char *value) { if (current) current->headers += String(name) + ": " + value + "\r\n"; }
  void send(int code, const char *type = "", const String &body = "") {
    if (!current) return;
    auto &e = *current; free(e.owned); e.owned = nullptr; e.data = nullptr; e.size = body.length(); e.code = code; e.type = type;
    if (e.size) {
      e.owned = static_cast<char *>(ps_malloc(e.size));
      if (!e.owned) e.owned = static_cast<char *>(malloc(e.size));
      if (e.owned) { memcpy(e.owned, body.c_str(), e.size); e.data = e.owned; }
      else { e.size = 0; e.code = 503; }
    }
  }
  void send_P(int code, const char *type, const char *data, size_t size) {
    if (!current) return;
    free(current->owned); current->owned = nullptr;
    current->code = code; current->type = type; current->data = data; current->size = size;
  }
};
