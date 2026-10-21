#pragma once
#include <cstddef>
#include <cstdint>

namespace telegram {
enum class StoredKind : uint32_t { Notice = 1, Alarm = 2, Report = 3 };
inline uint32_t checksum(const void *bytes, size_t size, uint32_t crc = 0xffffffff) {
  const auto *p = static_cast<const uint8_t *>(bytes);
  for (size_t i = 0; i < size; ++i) {
    crc ^= p[i]; for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return crc;
}
struct StoredHeader {
  uint32_t magic = 0x4f565447, version = 1;
  int64_t owner = 0;
  uint64_t utc = 0;
  uint64_t sequence = 0;
  StoredKind kind = StoredKind::Notice;
  uint32_t metadata = 0, payload = 0, crc = 0;
  bool valid(int64_t expectedOwner, size_t fileSize) const {
    return magic == 0x4f565447 && version == 1 && owner == expectedOwner && metadata > 0 &&
      metadata <= 16384 && payload <= 4321 * 32 &&
      (kind == StoredKind::Notice || kind == StoredKind::Alarm || kind == StoredKind::Report) &&
      fileSize == sizeof(*this) + metadata + payload;
  }
  uint32_t initialChecksum() const { return checksum(this, offsetof(StoredHeader, crc)); }
};
struct StoredProgress {
  uint32_t magic = 0x41434b31, index = 0, ambiguous = 0, failed = 0, crc = 0;
  void seal() { crc = checksum(this, offsetof(StoredProgress, crc)); }
  bool valid() const { return magic == 0x41434b31 && index <= 255 && ambiguous <= 1 && failed <= 1 && crc == checksum(this, offsetof(StoredProgress, crc)); }
};
} // namespace telegram
