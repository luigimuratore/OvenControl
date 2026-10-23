#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "profile.h"
#include "thermal_limits.h"

namespace programs {
constexpr uint32_t kVersion = 1;
constexpr size_t kMaxPrograms = 8, kMaxSteps = 12;
constexpr const char *kStorageKey = "programsV1";
enum class WriteStatus { Saved, NoSpace, Error };
template<class SaveProposed, class SaveApplied, class RetireLegacy>
bool saveWithLegacyMigration(bool legacy, SaveProposed proposed, SaveApplied applied, RetireLegacy retire) {
  const auto result = proposed();
  if (result == WriteStatus::Saved) { if (legacy) retire(); return true; }
  if (result != WriteStatus::NoSpace || !legacy || applied() != WriteStatus::Saved) return false;
  retire(); // A committed compact copy now protects the applied programs.
  return proposed() == WriteStatus::Saved;
}
// Fixed-width fields avoid persisting String pointers or compiler padding.
struct StoredStep { float target = 0, rate = 0; uint16_t minutes = 0; uint8_t type = 0, reserved = 0; };
struct StoredProgram {
  char id[33]{}, name[49]{};
  uint8_t count = 0, reserved = 0;
  StoredStep steps[kMaxSteps];
};
struct Snapshot { uint32_t version = kVersion, count = 0; StoredProgram programs[kMaxPrograms]; };
static_assert(sizeof(StoredStep) == 12 && sizeof(StoredProgram) == 228, "Stable program storage layout");
inline bool valid(const Snapshot &data) {
  if (data.version != kVersion || !data.count || data.count > kMaxPrograms) return false;
  for (size_t i = 0; i < data.count; ++i) {
    const auto &p = data.programs[i];
    if (!std::memchr(p.id, 0, sizeof(p.id)) || !p.id[0] || !std::memchr(p.name, 0, sizeof(p.name)) || !p.name[0] ||
        !p.count || p.count > kMaxSteps || p.reserved) return false;
    for (const char *c = p.id; *c; ++c)
      if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_')) return false;
    for (size_t j = 0; j < i; ++j) if (!std::strcmp(p.id, data.programs[j].id)) return false;
    for (size_t j = 0; j < p.count; ++j) {
      const auto &s = p.steps[j];
      if (s.type > uint8_t(profile::Type::Cooldown) || s.reserved || !thermal::validTarget(s.target)) return false;
      const auto type = profile::Type(s.type);
      if (type == profile::Type::Hold ? (s.minutes < 1 || s.minutes > 360) : (!std::isfinite(s.rate) || s.rate < .1f)) return false;
    }
  }
  return true;
}
inline size_t bytes(const Snapshot &data) {
  return offsetof(Snapshot, programs) + data.count * sizeof(StoredProgram);
}
// Version 2 stores only the names and steps actually used. Keep the V1
// decoder so an OTA never requires erasing the previous program collection.
constexpr uint32_t kPackedVersion = 2;
constexpr size_t kMaxBlobBytes = sizeof(Snapshot);
inline size_t encode(const Snapshot &data, void *blob, size_t capacity) {
  if (!blob || !valid(data)) return 0;
  size_t needed = 8;
  for (size_t i = 0; i < data.count; ++i) {
    const auto &p = data.programs[i];
    needed += 3 + std::strlen(p.id) + std::strlen(p.name);
    for (size_t j = 0; j < p.count; ++j)
      needed += p.steps[j].type == uint8_t(profile::Type::Hold) ? 7 : 9;
  }
  if (capacity < needed) return 0;
  auto *out = static_cast<uint8_t *>(blob);
  auto integer = [&](uint32_t value, size_t width) {
    for (size_t j = 0; j < width; ++j) *out++ = uint8_t(value >> (8 * j));
  };
  auto real = [&](float value) { uint32_t bits; std::memcpy(&bits, &value, 4); integer(bits, 4); };
  integer(kPackedVersion, 4); integer(data.count, 4);
  for (size_t i = 0; i < data.count; ++i) {
    const auto &p = data.programs[i]; const auto id = std::strlen(p.id), name = std::strlen(p.name);
    integer(id, 1); integer(name, 1); integer(p.count, 1);
    std::memcpy(out, p.id, id); out += id; std::memcpy(out, p.name, name); out += name;
    for (size_t j = 0; j < p.count; ++j) {
      const auto &s = p.steps[j]; integer(s.type, 1); real(s.target);
      if (s.type == uint8_t(profile::Type::Hold)) integer(s.minutes, 2); else real(s.rate);
    }
  }
  return needed;
}
inline bool decode(const void *blob, size_t length, Snapshot &out) {
  if (!blob || length < offsetof(Snapshot, programs) || length > sizeof(Snapshot)) return false;
  Snapshot candidate{};
  const auto *in = static_cast<const uint8_t *>(blob), *end = in + length;
  auto integer = [&](size_t width) {
    uint32_t value = 0;
    for (size_t j = 0; j < width; ++j) value |= uint32_t(*in++) << (8 * j);
    return value;
  };
  const uint32_t version = integer(4);
  if (version == kVersion) {
    std::memcpy(&candidate, blob, length);
    if (length != bytes(candidate) || !valid(candidate)) return false;
  } else if (version == kPackedVersion) {
    candidate.count = integer(4);
    if (!candidate.count || candidate.count > kMaxPrograms) return false;
    auto real = [&]() { const uint32_t bits = integer(4); float value; std::memcpy(&value, &bits, 4); return value; };
    for (size_t i = 0; i < candidate.count; ++i) {
      if (end - in < 3) return false;
      const size_t id = integer(1), name = integer(1), count = integer(1);
      auto &p = candidate.programs[i];
      if (!id || id >= sizeof(p.id) || !name || name >= sizeof(p.name) || !count || count > kMaxSteps ||
          size_t(end - in) < id + name) return false;
      // Embedded NULs would otherwise hide part of an ID/name from validation.
      if (std::memchr(in, 0, id + name)) return false;
      std::memcpy(p.id, in, id); in += id; std::memcpy(p.name, in, name); in += name; p.count = count;
      for (size_t j = 0; j < count; ++j) {
        if (end - in < 1) return false;
        auto &s = p.steps[j]; s.type = integer(1);
        const bool hold = s.type == uint8_t(profile::Type::Hold);
        if (s.type > uint8_t(profile::Type::Cooldown) || end - in < (hold ? 6 : 8)) return false;
        s.target = real();
        if (hold) s.minutes = integer(2); else s.rate = real();
      }
    }
    if (in != end || !valid(candidate)) return false;
  } else return false;
  out = candidate; return true;
}
} // namespace programs
