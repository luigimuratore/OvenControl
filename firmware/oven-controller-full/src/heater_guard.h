#pragma once
#include <cmath>
#include <cstdint>

namespace heater {
inline bool validSettings(bool ssr, float maxPower, uint32_t windowMs) {
  return std::isfinite(maxPower) && maxPower >= 1 && maxPower <= 100 &&
         windowMs >= (ssr ? 1000UL : 60000UL) && windowMs <= 300000UL;
}
enum class Fault : uint8_t { None, StaleControl, CycleTimeout, InvalidPlan };
struct Plan {
  bool cycleActive = false, heatingAllowed = false, healthy = false;
  uint32_t sampleAt = 0, cycleAt = 0, windowAt = 0, windowMs = 60000;
  float duty = 0;
};
class Guard {
 public:
  static constexpr uint32_t kStaleMs = 3000, kMaxCycleMs = 12UL * 3600000UL;
  Fault fault = Fault::None;
  bool on = false;
  void publish(const Plan &value) { plan = value; }
  void inhibit() { plan.cycleActive = false; plan.heatingAllowed = false; plan.duty = 0; on = false; }
  void clear() { plan = Plan{}; fault = Fault::None; on = false; }
  bool tick(uint32_t now) {
    on = false;
    if (fault != Fault::None || !plan.cycleActive) return false;
    if (uint32_t(now - plan.cycleAt) >= kMaxCycleMs) fault = Fault::CycleTimeout;
    else if (uint32_t(now - plan.sampleAt) >= kStaleMs) fault = Fault::StaleControl;
    else if (!plan.healthy || !std::isfinite(plan.duty) || plan.duty < 0 || plan.duty > 100 ||
             plan.windowMs < 1000 || plan.windowMs > 300000) fault = Fault::InvalidPlan;
    if (fault != Fault::None || !plan.heatingAllowed) return false;
    const uint32_t position = uint32_t(now - plan.windowAt) % plan.windowMs;
    on = position < uint32_t(plan.duty * plan.windowMs / 100.0f);
    return on;
  }
 private:
  Plan plan;
};
} // namespace heater
