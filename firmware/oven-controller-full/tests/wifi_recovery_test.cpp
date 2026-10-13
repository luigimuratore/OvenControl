#include <cassert>
#include <cstring>
#include <iostream>
#include "../src/wifi_recovery.h"

int main() {
  using network::Action;
  network::RetryPlan plan; plan.begin(100);
  // Leave the initial association and DHCP ample time before intervening.
  assert(plan.tick(30099, false) == Action::None);
  assert(plan.tick(30100, false) == Action::Reconnect);
  assert(plan.tick(60099, false) == Action::None);
  assert(plan.tick(60100, false) == Action::Reconnect);
  assert(plan.tick(120100, false) == Action::Reconnect);
  assert(plan.tick(240100, false) == Action::Reconfigure);
  // Router absent for two days: retries continue at bounded intervals.
  uint32_t now = 240100;
  for (unsigned n = 0; n < 1440; ++n) {
    now += 120000;
    assert(plan.tick(now - 1, false) == Action::None);
    assert(plan.tick(now, false) != Action::None);
  }
  // Never disconnect a healthy station. After a new loss wait five seconds.
  assert(plan.tick(now + 1, true) == Action::None);
  assert(plan.tick(now + 500000, true) == Action::None);
  now += 500001;
  assert(plan.tick(now, false) == Action::None);
  assert(plan.tick(now + 4999, false) == Action::None);
  assert(plan.tick(now + 5000, false) == Action::Reconnect);
  // OTA suspension must never perform a recovery operation.
  now += 1000000;
  assert(plan.tick(now, false, false) == Action::None);
  assert(plan.tick(now + 1000000, false, false) == Action::None);
  assert(plan.tick(now + 1000001, false, true) == Action::Reconnect);
  network::RetryPlan wrap; wrap.begin(UINT32_MAX - 10000);
  assert(wrap.tick(19998, false) == Action::None);
  assert(wrap.tick(19999, false) == Action::Reconnect);
  assert(strstr(network::reasonText(201), "Rete non trovata"));
  assert(strstr(network::reasonText(202), "Autenticazione rifiutata"));
  assert(strstr(network::reasonText(200), "Segnale"));
  std::cout << "Wi-Fi: initial grace, two-day outage, recovery, healthy station, OTA suspension and clock wrap passed\n";
}
