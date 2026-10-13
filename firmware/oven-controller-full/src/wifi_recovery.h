#pragma once
#include <cstdint>

namespace network {
enum class Action : uint8_t { None, Reconnect, Reconfigure };
// Called by the Wi-Fi task only. No waits, GPIO, credential or flash access.
class RetryPlan {
  uint32_t nextAt = 0, streak = 0;
  bool wasConnected = false;
public:
  void begin(uint32_t now) { nextAt = now + 30000; streak = 0; wasConnected = false; }
  uint32_t waitMs(uint32_t now) const {
    return int32_t(now - nextAt) >= 0 ? 0 : uint32_t(nextAt - now);
  }
  Action tick(uint32_t now, bool connected, bool allowed = true) {
    if (connected) { wasConnected = true; streak = 0; return Action::None; }
    if (wasConnected) { wasConnected = false; nextAt = now + 5000; }
    if (!allowed || waitMs(now)) return Action::None;
    ++streak;
    nextAt = now + (streak == 1 ? 30000 : streak == 2 ? 60000 : 120000);
    return streak % 4 == 0 ? Action::Reconfigure : Action::Reconnect;
  }
};
inline const char *reasonText(unsigned reason) {
  switch (reason) {
    case 0: return "Nessun errore di disconnessione registrato";
    case 2: return "Timeout autenticazione";
    case 15: case 204: return "Timeout scambio chiavi: segnale o sicurezza del router";
    case 200: return "Segnale/router non raggiungibile (beacon timeout)";
    case 201: return "Rete non trovata: disponibilita, banda 2,4 GHz o copertura";
    case 202: return "Autenticazione rifiutata: password o sicurezza del router";
    case 203: return "Associazione rifiutata dal router";
    default: return "Disconnessione Wi-Fi: vedere codice e log seriale";
  }
}
} // namespace network
