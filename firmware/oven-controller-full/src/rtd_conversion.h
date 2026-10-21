#pragma once
#include <cstdint>

namespace probes {
// Same timing and one-shot/fault detection sequence as Adafruit's readRTD +
// readFault(AUTO), but with no delay on the control task. Both chips progress
// in parallel. Device supplies bias/oneShot/readRaw/startFault/readFault/stop.
class Conversion {
  enum class Stage : uint8_t { Idle, Bias, Converting, Fault };
  Stage stage = Stage::Idle;
  uint32_t at = 0;
 public:
  uint16_t raw = 0;
  uint8_t fault = 0;
  template <class Device> void start(Device &device, uint32_t now) {
    device.bias(); at = now; stage = Stage::Bias;
  }
  template <class Device> bool tick(Device &device, uint32_t now) {
    if (stage == Stage::Bias && uint32_t(now - at) >= 10) {
      device.oneShot(); at = now; stage = Stage::Converting;
    } else if (stage == Stage::Converting && uint32_t(now - at) >= 65) {
      raw = device.readRaw(); device.startFault(); at = now; stage = Stage::Fault;
    } else if (stage == Stage::Fault && uint32_t(now - at) >= 1) {
      fault = device.faultBits(); device.stop(); stage = Stage::Idle; return true;
    }
    return false;
  }
};
} // namespace probes
