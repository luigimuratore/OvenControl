#include <cassert>
#include <iostream>
#include "../src/rtd_conversion.h"
#include "../src/sensor_recovery.h"
struct Device {
  unsigned biases=0,shots=0,rawReads=0,faultCycles=0,faultReads=0,stops=0;
  void bias(){++biases;} void oneShot(){++shots;}
  uint16_t readRaw(){++rawReads;return 8303;}void startFault(){++faultCycles;}
  uint8_t faultBits(){++faultReads;return 4;}void stop(){++stops;}
};
int main() {
  probes::Conversion a,b;Device d1,d2;
  const uint32_t start=UINT32_MAX-30;
  a.start(d1,start);b.start(d2,start);
  for(uint32_t elapsed=0;elapsed<76;++elapsed) {
    assert(!a.tick(d1,start+elapsed));assert(!b.tick(d2,start+elapsed));
    assert(d1.shots==unsigned(elapsed>=10));assert(d1.rawReads==unsigned(elapsed>=75));
  }
  assert(a.tick(d1,start+76)&&b.tick(d2,start+76));
  assert(a.raw==8303&&a.fault==4&&d1.stops==1&&d2.stops==1);
  assert(!a.tick(d1,start+100));a.start(d1,start+200);
  assert(!a.tick(d1,start+210));assert(!a.tick(d1,start+275));assert(a.tick(d1,start+276));
  probes::Recovery recovery;
  assert(recovery.begin(4)==probes::RecoveryResult::Pending);
  for(unsigned i=0;i<8;++i)assert(recovery.confirm(probes::Confirmation::OvUv)==probes::RecoveryResult::Pending);
  assert(recovery.confirm(probes::Confirmation::OvUv)==probes::RecoveryResult::Failed);
  assert(recovery.failure==probes::RecoveryFailure::ConsecutiveFaults);
  assert(recovery.begin(4)==probes::RecoveryResult::Pending);
  assert(recovery.confirm(probes::Confirmation::Safe)==probes::RecoveryResult::Pending);
  assert(recovery.confirm(probes::Confirmation::Safe)==probes::RecoveryResult::Confirmed);
  assert(recovery.begin(4)==probes::RecoveryResult::Pending);
  assert(recovery.confirm(probes::Confirmation::Unsafe)==probes::RecoveryResult::Failed);
  std::cout<<"Async PT100: parallel one-shots, 10/65/1 ms timing, rollover, repeats, ten-fault limit and two fresh confirmations passed\n";
}
