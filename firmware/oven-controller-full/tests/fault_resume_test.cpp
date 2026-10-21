#include "../src/fault_resume.h"
#include "../src/cycle_report.h"
#include <cassert>
#include <cstdio>

int main() {
  using namespace regulation;
  FaultResume checkpoint;
  ProgramPause pause;
  profile::HoldTimer hold;
  float origin = 0; uint32_t at = 0, before = 0;
  const profile::Step ramp{profile::Type::Ramp,175,2,0};
  assert(!checkpoint.restore(ramp,0,origin,at,before,hold));
  checkpoint.capture(ramp,2,false,pause,100,1000,90000,0,301000);
  assert(checkpoint.available && checkpoint.reference == 110 && checkpoint.step == 2);
  assert(checkpoint.restore(ramp,1201000,origin,at,before,hold));
  assert(origin == 100 && before == 90000 && profile::setpoint(ramp,origin,1201000-at) == 110);
  assert(before + uint32_t(1201000-at) == 390000);
  assert(checkpoint.safePair(119,true,121,true)); // Mean exactly +10 is permitted.
  assert(!checkpoint.safePair(120.01f,true,120.01f,true));
  assert(!checkpoint.safePair(100,true,100,false));
  assert(!checkpoint.safePair(NAN,true,100,true));
  assert(!checkpoint.safePair(415,true,-20,true)); // Individual hard limit.
  assert(checkpoint.safePair(90,true,100,true)); // Door opened: cold readings can recover.
  thermal::ProtectionSettings protections;
  assert(!checkpoint.safePair(80,true,120,true,protections)); // Mean is safe, delta exceeds 30.
  protections.probeDeltaEnabled=false;
  assert(checkpoint.safePair(80,true,120,true,protections));
  protections.meanOvershootEnabled=false;
  assert(checkpoint.safePair(130,true,130,true,protections));
  assert(!checkpoint.safePair(415,true,100,true,protections));

  const profile::Step cooling{profile::Type::Cooldown,25,1,0};
  const uint32_t start=UINT32_MAX-100000;
  checkpoint.capture(cooling,1,false,pause,175,start,0,0,start+60000);
  const uint32_t now=start+900000;
  assert(checkpoint.restore(cooling,now,origin,at,before,hold));
  assert(profile::setpoint(cooling,origin,uint32_t(now-at)) == 174);
  assert(checkpoint.safePair(195,true,195,true)); // Passive cooling can lag the reference.
  assert(!checkpoint.safePair(160,true,200,true)); // Probe delta remains enforced.
  assert(!checkpoint.safePair(415,true,415,true));

  const profile::Step holding{profile::Type::Hold,175,0,3};
  checkpoint.capture(holding,1,false,pause,175,1000,0,60000,62000);
  assert(checkpoint.restore(holding,662000,origin,at,before,hold));
  assert(hold.elapsedMs == 60000);
  hold.update(662000,true); assert(hold.elapsedMs == 60000);
  hold.update(662500,true); assert(hold.elapsedMs == 60500);

  assert(pause.begin(172,62000,1000,9000));
  checkpoint.capture(ramp,0,true,pause,100,1000,9000,0,200000);
  assert(checkpoint.reference == 172);
  assert(checkpoint.restore(ramp,500000,origin,at,before,hold));
  assert(origin == 172 && at == 500000 && before == 70000);

  manual::Session session,resumed;
  assert(session.start(175,2,100,100,1000));
  checkpoint.capture(session,301000);
  assert(checkpoint.reference == 110 && checkpoint.restore(1201000,resumed));
  assert(resumed.target == 175 && resumed.rateCMin == 2 && resumed.reference == 110);
  assert(resumed.startedAt == 1000 && resumed.elapsedMs(1201000) == 300000);
  resumed.tick(110,1261000); assert(resumed.reference == 112 && resumed.elapsedMs(1261000) == 360000);
  assert(resumed.cooldown(25,1,1261000));
  checkpoint.capture(resumed,1321000);
  assert(checkpoint.restore(1921000,session));
  assert(session.descending() && session.reference == 111 && session.target == 25);
  assert(checkpoint.safePair(150,true,150,true)); // Manual cooldown continuation uses the same exception.
  checkpoint.clear(); assert(!checkpoint.available && !checkpoint.restore(2000000,session));

  // Reopen the full report after a fault, preserving accumulated hold/stats.
  reports::Recorder report; report.begin(1000,&holding,1);
  report.observe(62000,175,175,true,true,175,8);
  assert(report.finish(62000,reports::Outcome::Fault,60000));
  const auto alarmReport=report.data;
  assert(report.continueAfterFault(662000));
  assert(!report.continueAfterFault(662000));
  assert(report.data.activeMs == 61000 && report.data.pausedMs == 600000 && report.data.pauseCount == 1);
  assert(report.data.steps[0].pausedMs == 600000 && report.data.stats.samples == alarmReport.stats.samples);
  assert(report.data.steps[0].holdInBandMs == 60000 && report.data.outcome == reports::Outcome::None);
  assert(alarmReport.outcome == reports::Outcome::Fault && alarmReport.endMs == 62000);
  report.advance(782000,180000); assert(report.finish(782000,reports::Outcome::Completed,180000));
  assert(report.data.completedSteps == 1 && report.data.activeMs == 181000);
  assert(!report.canContinue());
  FaultResume afterReboot; assert(!afterReboot.available);
  puts("Fault continuation: ramp/cooldown freeze, retained hold, manual time, fresh qualification, report pauses and millis wrap passed");
}
