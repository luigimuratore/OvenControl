#pragma once
#include <cstdint>
#include <cstring>

namespace telegram {
constexpr size_t kMessageBytes = 2400;
constexpr uint32_t kCommandAgeSec = 120;
constexpr uint32_t kReplyLifetimeMs = 10000;
constexpr uint32_t kNoticeLifetimeMs = 3600000;
constexpr const char *kGuideText = R"guide(📌 OVENCONTROL · GUIDA RAPIDA

📋 COMANDI
/status — Stato completo: ciclo, temperature, uscite, PID e allarmi.
/help — Aiuto sul bot.
/start — Mostra e fissa questa guida; non avvia il forno.

🔔 EVENTI AUTOMATICI
🚀 Avvio · ⏸️ Pausa · ⏯️ Ripresa · ⚠️ STOP · ✅ Fine ciclo.
🚨 Emergenze, errori e blackout: avvisi dettagliati.
📄 Report PDF al termine di ogni ciclo.

Gli eventi ordinari e i PDF sono silenziosi. Lascia attive le notifiche della chat per ricevere gli allarmi.
Il blackout viene segnalato al ritorno di corrente e Internet.

Avvio, STOP, emergenza e riconoscimento degli allarmi si comandano dalla dashboard locale.)guide";

inline bool validToken(const char *token) {
  size_t i = 0;
  while (token[i] >= '0' && token[i] <= '9') ++i;
  if (!i || token[i++] != ':') return false;
  const size_t start = i;
  for (; token[i]; ++i) {
    const char c = token[i];
    if (i >= 160 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return i - start >= 20;
}
inline bool authorized(int64_t configured, int64_t chat, int64_t sender, const char *type) {
  return configured > 0 && chat == configured && sender == configured &&
         type && std::strcmp(type, "private") == 0;
}
inline bool fresh(int64_t date, int64_t now) {
  return date > 0 && now >= date && now - date <= kCommandAgeSec;
}
enum class Command { None, Status, Help };
inline Command command(const char *text) {
  if (!text) return Command::None;
  if (std::strcmp(text, "/status") == 0) return Command::Status;
  if (std::strcmp(text, "/start") == 0 || std::strcmp(text, "/help") == 0) return Command::Help;
  return Command::None;
}
inline bool expired(uint32_t now, uint32_t created, bool reply, bool critical) {
  if (critical) return false; // Keep alarms until delivery or reboot.
  return uint32_t(now - created) > (reply ? kReplyLifetimeMs : kNoticeLifetimeMs);
}
inline uint32_t retryMs(unsigned failures, unsigned serverSeconds = 0) {
  const uint32_t seconds = failures <= 1 ? 5 : failures == 2 ? 15 : failures == 3 ? 30 : 60;
  // Cap to keep wrap-safe millis arithmetic (at most 24 hours).
  return (serverSeconds > seconds ? (serverSeconds > 86400 ? 86400 : serverSeconds) : seconds) * 1000UL;
}
} // namespace telegram
