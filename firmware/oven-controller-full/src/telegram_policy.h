#pragma once
#include <cstdint>
#include <cstring>

namespace telegram {
constexpr size_t kMessageBytes = 2400;
constexpr uint32_t kCommandAgeSec = 120;
constexpr uint32_t kReplyLifetimeMs = 10000;
constexpr uint32_t kNoticeLifetimeMs = 3600000;
enum class Mode : uint8_t { Essential, Brief, Detailed };
inline bool validMode(int value) { return value >= 0 && value <= 2; }
inline Mode modeFrom(int value) { return validMode(value) ? Mode(value) : Mode::Brief; }
inline const char *modeName(Mode mode) {
  return mode == Mode::Essential ? "Essenziale" : mode == Mode::Detailed ? "Dettagliata" : "Sintetica";
}
inline const char *modeKey(Mode mode) {
  return mode == Mode::Essential ? "essenziale" : mode == Mode::Detailed ? "dettagliata" : "sintetica";
}
enum class EventScope : uint8_t { Selected, Always, DetailedOnly };
inline bool allowEvent(Mode mode, bool critical, EventScope scope = EventScope::Selected) {
  return critical || scope == EventScope::Always ||
    (scope == EventScope::DetailedOnly ? mode == Mode::Detailed : mode != Mode::Essential);
}
constexpr const char *kEssentialButton = "🛡️ Essenziale";
constexpr const char *kBriefButton = "🔔 Sintetica";
constexpr const char *kDetailedButton = "📋 Dettagliata";
constexpr const char *kModeMenuText = R"modes(Scegli come ricevere gli eventi:
🛡️ Essenziale — Allarmi/blackout e PDF; avvio ESP e controllo sempre visibili.
🔔 Sintetica — Eventi brevi, allarmi dettagliati e PDF.
📋 Dettagliata — Tutti gli eventi con stato completo e PDF.

/status e /plot restano sempre disponibili. Avvio ESP e avvio del controllo vengono sempre segnalati, senza suono. Gli eventi ordinari e i PDF restano silenziosi; gli allarmi mantengono la notifica sonora.)modes";
constexpr const char *kGuideText = R"guide(📌 OVENCONTROL · GUIDA RAPIDA

📋 COMANDI
/status — Stato completo: ciclo, temperature, uscite, PID e allarmi.
/plot — Immagine aggiornata del grafico target/media PT100 (ultimi 30 minuti).
/help — Aiuto sul bot.
/start — Mostra e fissa questa guida; non avvia il forno.
/modalita — Mostra la modalità attiva e i pulsanti di scelta.

🎛 MODALITÀ NOTIFICHE
🛡️ /essenziale — Allarmi/blackout e PDF, più avvio ESP e controllo.
🔔 /sintetica — Eventi brevi, allarmi dettagliati e PDF.
📋 /dettagliata — Tutti gli eventi con stato completo e PDF.
La scelta viene salvata; /status e /plot restano sempre disponibili. Avvio ESP e avvio del controllo vengono sempre segnalati, senza suono.

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
enum class Command { None, Status, Plot, Help, ModeMenu, Essential, Brief, Detailed };
inline Command command(const char *text) {
  if (!text) return Command::None;
  if (std::strcmp(text, "/status") == 0) return Command::Status;
  if (std::strcmp(text, "/plot") == 0) return Command::Plot;
  if (std::strcmp(text, "/start") == 0 || std::strcmp(text, "/help") == 0) return Command::Help;
  if (std::strcmp(text, "/modalita") == 0) return Command::ModeMenu;
  if (std::strcmp(text, "/essenziale") == 0 || std::strcmp(text, kEssentialButton) == 0) return Command::Essential;
  if (std::strcmp(text, "/sintetica") == 0 || std::strcmp(text, kBriefButton) == 0) return Command::Brief;
  if (std::strcmp(text, "/dettagliata") == 0 || std::strcmp(text, kDetailedButton) == 0) return Command::Detailed;
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
