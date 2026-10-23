#pragma once
#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <cstdlib>
#include <cstring>
#include "storage_migration.h"

namespace ovenstorage {
struct Initialization {
  bool present = false, ready = false;
  esp_err_t error = ESP_OK;
  size_t copiedKeys = 0;
  const char *partition() const { return ready ? kPartition : "nvs"; }
};
class NvsMigration {
  nvs_handle_t source = 0, target = 0, meta = 0;
  esp_err_t status = ESP_OK;
  size_t copied = 0;
  bool ok(esp_err_t value) { status = value; return value == ESP_OK; }
  // Use the exact original NVS types, including signed integers, strings and
  // blobs. No list of selected keys can accidentally omit a safety latch.
  bool transfer(const nvs_entry_info_t &entry, bool verify) {
    const char *key = entry.key;
    switch (entry.type) {
#define OVEN_NVS_VALUE(TAG, TYPE, API) \
      case NVS_TYPE_##TAG: { TYPE from{}, to{}; \
        if (!ok(nvs_get_##API(source, key, &from))) return false; \
        if (!verify) return ok(nvs_set_##API(target, key, from)); \
        if (!ok(nvs_get_##API(target, key, &to))) return false; \
        return ok(from == to ? ESP_OK : ESP_ERR_INVALID_CRC); }
      OVEN_NVS_VALUE(I8, int8_t, i8)
      OVEN_NVS_VALUE(U8, uint8_t, u8)
      OVEN_NVS_VALUE(I16, int16_t, i16)
      OVEN_NVS_VALUE(U16, uint16_t, u16)
      OVEN_NVS_VALUE(I32, int32_t, i32)
      OVEN_NVS_VALUE(U32, uint32_t, u32)
      OVEN_NVS_VALUE(I64, int64_t, i64)
      OVEN_NVS_VALUE(U64, uint64_t, u64)
#undef OVEN_NVS_VALUE
      case NVS_TYPE_STR: case NVS_TYPE_BLOB: break;
      default: return ok(ESP_ERR_NVS_TYPE_MISMATCH);
    }
    const bool string = entry.type == NVS_TYPE_STR;
    size_t size = 0;
    if (!ok(string ? nvs_get_str(source, key, nullptr, &size) : nvs_get_blob(source, key, nullptr, &size))) return false;
    // The source partition is only 20 KiB; bound allocations even for corruption.
    if (size > 0x5000) return ok(ESP_ERR_NVS_INVALID_LENGTH);
    void *from = std::malloc(size ? size : 1), *to = verify ? std::malloc(size ? size : 1) : nullptr;
    if (!from || (verify && !to)) { std::free(from); std::free(to); return ok(ESP_ERR_NO_MEM); }
    size_t actual = size;
    esp_err_t result = string ? nvs_get_str(source, key, static_cast<char *>(from), &actual) : nvs_get_blob(source, key, from, &actual);
    if (result == ESP_OK && actual != size) result = ESP_ERR_NVS_INVALID_LENGTH;
    if (result == ESP_OK && !verify)
      result = string ? nvs_set_str(target, key, static_cast<const char *>(from)) : nvs_set_blob(target, key, from, size);
    if (result == ESP_OK && verify) {
      actual = size;
      result = string ? nvs_get_str(target, key, static_cast<char *>(to), &actual) : nvs_get_blob(target, key, to, &actual);
      if (result == ESP_OK && (actual != size || std::memcmp(from, to, size))) result = ESP_ERR_INVALID_CRC;
    }
    std::free(from); std::free(to); return ok(result);
  }
  bool visit(bool verify) {
    if (!source) return true; // Fresh installation has no legacy namespace.
    size_t used = 0, visited = 0;
    if (!ok(nvs_get_used_entry_count(source, &used))) return false;
    auto iterator = nvs_entry_find("nvs", kNamespace, NVS_TYPE_ANY);
    // IDF 4.x also returns null on iterator allocation failure. Never interpret
    // that as an empty source when its namespace still contains entries.
    if (!iterator && used) return ok(ESP_ERR_NO_MEM);
    while (iterator) {
      nvs_entry_info_t entry{}; nvs_entry_info(iterator, &entry);
      if (!transfer(entry, verify)) { nvs_release_iterator(iterator); return false; }
      if (!verify) ++copied;
      ++visited;
      iterator = nvs_entry_next(iterator);
    }
    if (verify && visited != copied) return ok(ESP_ERR_INVALID_CRC);
    return true;
  }
 public:
  ~NvsMigration() { if (source) nvs_close(source); if (target) nvs_close(target); if (meta) nvs_close(meta); }
  bool open() {
    if (!ok(nvs_flash_init_partition(kPartition)) ||
        !ok(nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &target)) ||
        !ok(nvs_open_from_partition(kPartition, "oven-meta", NVS_READWRITE, &meta))) return false;
    return true;
  }
  bool marker(bool &complete) {
    uint32_t version = 0; const auto result = nvs_get_u32(meta, "migration", &version);
    if (result == ESP_ERR_NVS_NOT_FOUND) { complete = false; return true; }
    if (!ok(result)) return false;
    complete = version == kMigrationVersion;
    return ok(complete ? ESP_OK : ESP_ERR_NVS_NEW_VERSION_FOUND);
  }
  bool resetTarget() { return ok(nvs_erase_all(target)) && ok(nvs_commit(target)); }
  bool copy() {
    const auto result = nvs_open_from_partition("nvs", kNamespace, NVS_READONLY, &source);
    if (result == ESP_ERR_NVS_NOT_FOUND) { source = 0; return true; }
    return ok(result) && visit(false);
  }
  bool commitTarget() { return ok(nvs_commit(target)); }
  bool verify() { return visit(true); }
  bool complete() { return ok(nvs_set_u32(meta, "migration", kMigrationVersion)) && ok(nvs_commit(meta)); }
  esp_err_t error() const { return status; }
  size_t count() const { return copied; }
};
inline Initialization initialize() {
  Initialization result;
  result.present = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, kPartition);
  if (!result.present) return result; // OTA alone cannot install a partition table.
  NvsMigration migration;
  result.ready = migration.open() && migrate(migration);
  result.error = migration.error(); result.copiedKeys = migration.count();
  return result;
}
} // namespace ovenstorage
