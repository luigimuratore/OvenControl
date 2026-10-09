#include <cassert>
#include <cstring>
#include <iostream>
#include "../src/power_recovery.h"
#include "../src/output_supervisor.h"

struct Store {
  recovery::Record committed;
  bool exists = false, fail = false;
  int load(recovery::Record &record) { if (!exists) return 0; record = committed; return 1; }
  bool save(const recovery::Record &record) { if (fail) return false; committed = record; exists = true; return true; }
};
int main() {
  Store nvs; recovery::Journal live;
  assert(live.load(nvs)); assert(live.boot(nvs, false, 0, 1));
  assert(!live.data.pending && !live.data.active);
  assert(live.begin(nvs, "Programma ceramica", 1791543600));
  assert(live.checkpoint(nvs, 1791543630));
  // A sudden power cut loses all RAM. No end-of-cycle save is possible.
  recovery::Journal reboot; assert(reboot.load(nvs));
  assert(reboot.boot(nvs, true, 0, 1));
  assert(reboot.data.pending && !reboot.data.active);
  assert(!strcmp(reboot.data.program, "Programma ceramica"));
  OutputSupervisor outputs; outputs.blockRecovery();
  heater::Plan p; p.cycleActive = p.heatingAllowed = p.healthy = true;
  p.sampleAt = p.cycleAt = p.windowAt = 100; p.windowMs = 1000; p.duty = 100;
  assert(!outputs.publish(p)); assert(!outputs.enterTest(false));
  assert(!outputs.startTest(TestMode::RelayPulse, 100, 1000, false));
  outputs.tick(100, true, false); assert(!outputs.relay && !outputs.red && !outputs.green);
  assert(!reboot.begin(nvs, "Altro programma", 1791543700));
  assert(reboot.returned(nvs, 1791543930));
  const auto bound = recovery::estimate(reboot.data.checkpointUtc, reboot.data.rebootUtc);
  assert(bound.known && bound.upperSec == 300);
  // A second reset retains both the latch and the first return time/cause.
  recovery::Journal twice; assert(twice.load(nvs)); assert(twice.boot(nvs, false, 0, 3));
  assert(twice.data.pending && twice.data.resetReason == 1 && twice.data.rebootUtc == 1791543930);
  nvs.fail = true; assert(!twice.acknowledge(nvs)); assert(twice.data.pending);
  recovery::Journal afterFailedAck; assert(afterFailedAck.load(nvs));
  assert(!afterFailedAck.boot(nvs, false, 0, 1)); assert(afterFailedAck.data.pending);
  nvs.fail = false; assert(afterFailedAck.acknowledge(nvs));
  outputs.acknowledgeRecovery(); outputs.tick(200, true, false); assert(!outputs.relay);
  recovery::Journal acknowledged; assert(acknowledged.load(nvs));
  assert(acknowledged.boot(nvs, false, 0, 1)); assert(!acknowledged.data.pending);
  assert(acknowledged.begin(nvs, "Nuovo programma", 1791544000));
  assert(acknowledged.finish(nvs));
  recovery::Journal normalStop; assert(normalStop.load(nvs)); assert(normalStop.boot(nvs, false, 0, 1));
  assert(!normalStop.data.pending);
  // Initial start is committed before energizing; even a cut between legacy
  // keys must block conservatively. Failed STOP writes retain the same block.
  assert(normalStop.begin(nvs, "Avvio appena richiesto", 1791545000));
  recovery::Journal earlyCut; assert(earlyCut.load(nvs)); assert(earlyCut.boot(nvs, false, 0, 9));
  assert(earlyCut.data.pending);
  assert(earlyCut.acknowledge(nvs)); assert(earlyCut.begin(nvs, "Ciclo", 0));
  nvs.fail = true; assert(!earlyCut.finish(nvs)); assert(earlyCut.data.pending);
  recovery::Journal failedStop; assert(failedStop.load(nvs)); assert(!failedStop.boot(nvs, false, 0, 1));
  assert(failedStop.data.pending);
  nvs.fail = false;
  Store old; recovery::Journal migration; assert(migration.load(old));
  assert(migration.boot(old, true, 1791543600, 1)); assert(migration.data.pending);
  assert(migration.data.checkpointUtc == 1791543600 && !migration.data.program[0]);
  Store corrupt; corrupt.exists = true; corrupt.committed.version = 99;
  recovery::Journal damaged; assert(!damaged.load(corrupt)); assert(damaged.data.pending);
  assert(damaged.boot(corrupt, false, 0, 1)); assert(damaged.data.pending);
  assert(!recovery::estimate(0, 1791543930).known);
  assert(!recovery::estimate(1791543930, 1791543900).known);
  assert(recovery::estimate(1791543930, 1791543930).known); // Zero is known, not missing.
  assert(!recovery::powerRelated(recovery::Cause::Other));
  assert(recovery::powerRelated(recovery::Cause::PowerOn));
  assert(recovery::powerRelated(recovery::Cause::Brownout));
  std::cout << "Power recovery: abrupt cuts, repeated resets, persistence failures, legacy migration, cold outputs, manual acknowledgement and time estimates passed\n";
}
