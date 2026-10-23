#pragma once
#include <cstdint>

namespace ovenstorage {
constexpr const char *kPartition = "oven_nvs";
constexpr const char *kNamespace = "oven-full";
constexpr uint32_t kMigrationVersion = 1;
// The target is private to this migration until the completion marker commits.
// Retrying an interrupted copy never modifies the source or trusts half a copy.
template<class Backend> bool migrate(Backend &store) {
  bool complete = false;
  if (!store.marker(complete)) return false;
  if (complete) return true;
  return store.resetTarget() && store.copy() && store.commitTarget() &&
    store.verify() && store.complete();
}
} // namespace ovenstorage
