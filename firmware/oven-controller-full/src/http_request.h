#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cctype>

namespace ovenhttp {
enum class Method : uint8_t { Get, Post, Head };
inline bool safetyAction(const char *uri, const char *action) {
  const size_t n = strcspn(uri, "?");
  const bool command = n == strlen("/api/command") && !memcmp(uri, "/api/command", n);
  const bool manual = n == strlen("/api/manual") && !memcmp(uri, "/api/manual", n);
  const bool test = n == strlen("/api/test/command") && !memcmp(uri, "/api/test/command", n);
  return ((command || manual || test) && !strcmp(action, "stop")) ||
    (command && !strcmp(action, "emergency")) || (test && !strcmp(action, "exit"));
}
// Bounded incremental HTTP framing. Only complete requests can reach control
// callbacks. No Stream::readString/readBytes timeout runs on the control task.
struct Request {
  static constexpr size_t kHeaderLimit = 4096, kBodyLimit = 16000, kUriLimit = 1024;
  char bytes[kHeaderLimit + kBodyLimit + 1]{};
  char uri[kUriLimit + 1]{};
  size_t used = 0, headerBytes = 0, bodyBytes = 0;
  Method method = Method::Get;
  int error = 0;
  bool complete = false;

  static bool equal(const char *a, size_t n, const char *b) {
    if (n != strlen(b)) return false;
    for (size_t i = 0; i < n; ++i)
      if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
  }
  void reset() { used = headerBytes = bodyBytes = 0; error = 0; complete = false; uri[0] = bytes[0] = 0; }
  bool parseHeader() {
    const char *lineEnd = strstr(bytes, "\r\n");
    const char *space = strchr(bytes, ' ');
    if (!lineEnd || !space || space >= lineEnd) { error = 400; return false; }
    if (equal(bytes, size_t(space - bytes), "GET")) method = Method::Get;
    else if (equal(bytes, size_t(space - bytes), "POST")) method = Method::Post;
    else if (equal(bytes, size_t(space - bytes), "HEAD")) method = Method::Head;
    else { error = 405; return false; }
    const char *version = static_cast<const char *>(memchr(space + 1, ' ', size_t(lineEnd - space - 1)));
    if (!version || version == space + 1 || space[1] != '/' || size_t(version - space - 1) > kUriLimit ||
        !(equal(version + 1, size_t(lineEnd - version - 1), "HTTP/1.1") || equal(version + 1, size_t(lineEnd - version - 1), "HTTP/1.0"))) {
      error = 400; return false;
    }
    memcpy(uri, space + 1, size_t(version - space - 1)); uri[version - space - 1] = 0;
    bool lengthSeen = false;
    for (const char *p = lineEnd + 2; p < bytes + headerBytes - 2;) {
      const char *end = strstr(p, "\r\n");
      if (!end || end == p) break;
      const char *colon = static_cast<const char *>(memchr(p, ':', size_t(end - p)));
      if (!colon || colon == p || *p == ' ' || *p == '\t') { error = 400; return false; }
      if (equal(p, size_t(colon - p), "Transfer-Encoding")) { error = 400; return false; }
      if (equal(p, size_t(colon - p), "Content-Length")) {
        if (lengthSeen) { error = 400; return false; }
        lengthSeen = true;
        const char *v = colon + 1; while (v < end && (*v == ' ' || *v == '\t')) ++v;
        if (v == end || *v < '0' || *v > '9') { error = 400; return false; }
        for (; v < end && *v >= '0' && *v <= '9'; ++v) {
          bodyBytes = bodyBytes * 10 + unsigned(*v - '0');
          if (bodyBytes > kBodyLimit) { error = 413; return false; }
        }
        while (v < end && (*v == ' ' || *v == '\t')) ++v;
        if (v != end) { error = 400; return false; }
      }
      p = end + 2;
    }
    if (method != Method::Post && bodyBytes) { error = 400; return false; }
    return true;
  }
  // Consume a single fragment. Pipelined trailing bytes are never executed.
  void feed(const char *data, size_t n) {
    for (size_t i = 0; i < n && !error && !complete; ++i) {
      if (!data[i]) { error = 400; break; }
      if (used == sizeof(bytes) - 1) { error = 413; break; }
      bytes[used++] = data[i]; bytes[used] = 0;
      if (!headerBytes) {
        if (used >= 4 && !memcmp(bytes + used - 4, "\r\n\r\n", 4)) {
          headerBytes = used;
          if (!parseHeader()) break;
        } else if (used >= kHeaderLimit) { error = 431; break; }
      }
      if (headerBytes && used == headerBytes + bodyBytes) complete = true;
    }
  }
  const char *body() const { return bytes + headerBytes; }
};
} // namespace ovenhttp
