#include "../src/storage_migration.h"
#include <cassert>
#include <iostream>
#include <map>
#include <string>

struct Disk {
  std::map<std::string, std::string> source{{"programsV1", "programmi"}, {"tgUsers", "owner+ospiti"},
    {"cycleSafety", "pending-interruption"}, {"emergency", "1"}, {"pid", "12/.02/50"}, {"unknown", "preservare"}};
  std::map<std::string, std::string> target;
  bool committed = false, marked = false;
  int failAt = -1, calls = 0;
  bool step() { return ++calls != failAt; }
  bool marker(bool &done) { done = marked; return step(); }
  bool resetTarget() { if (!step()) return false; target.clear(); committed = false; return true; }
  bool copy() {
    for (const auto &entry : source) { if (!step()) return false; target.insert(entry); }
    return true;
  }
  bool commitTarget() { if (!step()) return false; committed = true; return true; }
  bool verify() {
    for (const auto &entry : source) {
      if (!step() || target.count(entry.first) != 1 || target[entry.first] != entry.second) return false;
    }
    return true;
  }
  bool complete() { if (!step()) return false; assert(committed && target == source); marked = true; return true; }
};
int main() {
  Disk baseline; const auto source = baseline.source;
  assert(ovenstorage::migrate(baseline) && baseline.marked && baseline.target == source);
  const auto steps = baseline.calls;
  // Every failure boundary is retried as after a reboot; the only authoritative
  // collection stays untouched until a verified complete copy has committed.
  for (int fail = 1; fail <= steps; ++fail) {
    Disk disk; disk.failAt = fail;
    assert(!ovenstorage::migrate(disk) && !disk.marked && disk.source == source);
    disk.failAt = -1; disk.calls = 0;
    assert(ovenstorage::migrate(disk) && disk.marked && disk.target == source);
  }
  baseline.target["tgUsers"] = "nuovo utente";
  baseline.source["pid"] = "legacy obsoleto";
  baseline.calls = 0;
  assert(ovenstorage::migrate(baseline) && baseline.calls == 1);
  assert(baseline.target["tgUsers"] == "nuovo utente"); // Never replay stale legacy data after migration.
  Disk fresh; fresh.source.clear(); assert(ovenstorage::migrate(fresh) && fresh.target.empty());
  std::cout << "Dedicated NVS migration: every interrupted boundary, all keys/safety latches, verified commit, retry, fresh installation and no stale replay passed\n";
}
