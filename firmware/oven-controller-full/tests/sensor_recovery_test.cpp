#include <cassert>
#include <iostream>
#include "../src/sensor_recovery.h"
#include "../src/output_supervisor.h"
int main() {
  using probes::Confirmation;
  probes::RecoverySettings settings;
  assert(probes::validSettings(settings) && settings.bothFaultTimeoutSec==20);
  settings.version=1; assert(!probes::validSettings(settings)); settings.version=2;
  for(unsigned value:{0U,301U,UINT32_MAX}) {settings.bothFaultTimeoutSec=value;assert(!probes::validSettings(settings));}
  for(unsigned timeout:{1U,20U,300U}) {
    settings.bothFaultTimeoutSec=timeout;assert(probes::validSettings(settings));
    probes::Availability state;
    state.observe(100,true);assert(!state.waiting() && !state.expired(100000,timeout));
    state.observe(500,false);assert(state.waiting());
    for(unsigned ms=500;ms<500+timeout*1000;ms+=100) {state.observe(ms,false);assert(!state.expired(ms,timeout));}
    assert(state.expired(500+timeout*1000,timeout));
    state.observe(500+timeout*1000,true);assert(!state.waiting() && !state.expired(999999,timeout));
    state.observe(1000000,false);assert(!state.expired(1000000+timeout*1000-1,timeout));
    const uint32_t start=UINT32_MAX-500;
    state.observe(0,true);state.observe(start,false);
    assert(!state.expired(uint32_t(start+timeout*1000-1),timeout));
    assert(state.expired(uint32_t(start+timeout*1000),timeout));
  }
  // Every status mask is discarded. Neither its RAW value nor a stale old
  // temperature may participate in regulation when the other PT100 is good.
  for(unsigned mask=1;mask<=255;++mask) for(bool diagnostic:{false,true}) {
    assert(probes::confirmation(mask,11678,175,diagnostic)==Confirmation::Invalid);
    for(unsigned good=0;good<2;++good) {
      const thermal::Readings values(good?999.f:100.f,!good,good?100.f:-999.f,good);
      assert(values.count==1 && values.healthy() && values.mean==100 && values.hottest==100 && values.coldest==100);
      assert(!std::isfinite(values.delta) && values.inBand(100,5));
      assert(!values.inBand(106,5));
    }
  }
  assert(probes::confirmation(0,0,25)==Confirmation::Invalid);
  assert(probes::confirmation(0,32767,25)==Confirmation::Invalid);
  assert(probes::confirmation(0,8303,25,false,true)==Confirmation::Invalid);
  assert(probes::confirmation(0,8303,NAN)==Confirmation::Invalid);
  assert(probes::confirmation(0,8303,-21)==Confirmation::Invalid);
  assert(probes::confirmation(0,8303,-21,true)==Confirmation::Safe);
  assert(probes::confirmation(0,8303,415)==Confirmation::Unsafe);
  assert(probes::confirmation(0,8303,450)==Confirmation::Unsafe);
  assert(probes::confirmation(0x0c,8303,450)==Confirmation::Invalid);
  assert(!thermal::Readings(415,true,25,true).healthy());
  assert(!thermal::Readings(415,true,NAN,false).healthy());
  assert(thermal::Readings(415,false,25,true).healthy());
  assert(!thermal::Readings(NAN,true,NAN,true).healthy());
  assert(thermal::Readings(90,true,110,true).mean==100);
  // All outputs stop immediately when both are missing; the independent guard
  // never receives a healthy heating plan with zero available measurements.
  OutputSupervisor outputs; heater::Plan plan;
  plan.cycleActive=plan.heatingAllowed=plan.healthy=true;
  plan.sampleAt=plan.windowAt=100;plan.windowMs=1000;plan.duty=100;
  outputs.publish(plan);outputs.tick(100,true,false);assert(outputs.relay);
  probes::Availability availability;availability.observe(200,false);
  outputs.beginSensorCheck();assert(!outputs.relay);
  for(uint32_t now=200;now<20200;now+=100) {
    outputs.publish(plan);outputs.tick(now,true,false);
    assert(!outputs.relay && outputs.guard.fault==heater::Fault::None && !availability.expired(now,20));
  }
  assert(availability.expired(20200,20));
  // Recovery of either PT100 resets the entire absence interval and permits a
  // fresh plan; its sibling can remain failed indefinitely without an alarm.
  availability.observe(20200,true);outputs.endSensorCheck();plan.sampleAt=20200;
  outputs.publish(plan);outputs.tick(20200,true,false);assert(outputs.relay);
  for(uint32_t now=20300;now<120000;now+=100) {availability.observe(now,true);assert(!availability.expired(now,20));}
  assert(outputs.startTest(TestMode::RelayPulse,120000,1000,false));
  outputs.tick(120000,true,false);assert(outputs.relay);
  outputs.beginSensorCheck();outputs.tick(120001,true,false);assert(!outputs.relay && !outputs.red && !outputs.green);
  std::cout<<"PT100 availability: all fault masks, one-probe regulation, immediate OFF for both missing, exact configurable 1/20/300 s deadlines, continuous-loss reset, wrap and hard cutoff passed\n";
}
