#pragma once
#include <cstdint>

namespace probes {
// Nonblocking one-shots on both chips. At 50 Hz the datasheet gives 66 ms
// maximum conversion time: 65 ms can read the previous conversion. Manual
// fault timing also gives external input filters longer to settle than AUTO's
// fixed 100 us. Device supplies short register transfers, never delay().
class Conversion {
  enum class Stage : uint8_t { Idle, Bias, Converting, FaultOpen, FaultFinish };
  Stage stage = Stage::Idle;
  uint32_t at = 0;
 public:
  static constexpr uint32_t kBiasMs = 12, kConversionMs = 70, kFaultSettleMs = 5, kFaultFinishMs = 1;
  uint16_t raw = 0;
  uint8_t fault = 0;
  bool rawFault = false;
  template <class Device> void start(Device &device, uint32_t now) {
    device.bias(); at = now; stage = Stage::Bias;
  }
  template <class Device> bool tick(Device &device, uint32_t now) {
    if (stage == Stage::Bias && uint32_t(now - at) >= kBiasMs) {
      device.oneShot(); at = now; stage = Stage::Converting;
    } else if (stage == Stage::Converting && uint32_t(now - at) >= kConversionMs) {
      const uint16_t rtd = device.readRaw(); raw = rtd >> 1; rawFault = rtd & 1;
      device.startFault(); at = now; stage = Stage::FaultOpen;
    } else if (stage == Stage::FaultOpen && uint32_t(now - at) >= kFaultSettleMs) {
      device.finishFault(); at = now; stage = Stage::FaultFinish;
    } else if (stage == Stage::FaultFinish && uint32_t(now - at) >= kFaultFinishMs) {
      fault = device.faultBits(); device.stop(); stage = Stage::Idle; return true;
    }
    return false;
  }
};
} // namespace probes
