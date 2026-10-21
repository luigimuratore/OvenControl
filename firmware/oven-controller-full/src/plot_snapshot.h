#pragma once
#include "report_pdf.h"

namespace plotpng {
constexpr uint32_t kWindowMs = 30UL * 60000UL;
template <typename Sample>
size_t snapshot(const Sample *curve, size_t capacity, size_t head, size_t count,
                const Sample &current, reportpdf::Point *points, size_t pointCapacity, uint32_t windowMs = kWindowMs) {
  if (!curve || !capacity || count > capacity || !points || pointCapacity < 2 || !windowMs || windowMs > 3600000UL) return 0;
  size_t recent = 0;
  for (size_t i = 0; i < count; ++i)
    if (uint32_t(current.ms - curve[(head + capacity - count + i) % capacity].ms) <= windowMs) ++recent;
  size_t skip = recent > pointCapacity - 1 ? recent - (pointCapacity - 1) : 0, used = 0;
  const auto copy = [](const Sample &s) {
    return reportpdf::Point{s.ms, (s.flags & 3) == 3 ? (s.t1 + s.t2) * .5f : NAN,
      s.flags & 8 ? s.setpoint : NAN, s.step, s.flags};
  };
  for (size_t i = 0; i < count; ++i) {
    const auto &sample = curve[(head + capacity - count + i) % capacity];
    if (uint32_t(current.ms - sample.ms) > windowMs) continue;
    if (skip) { --skip; continue; }
    points[used++] = copy(sample);
  }
  if (used && points[used - 1].ms == current.ms) points[used - 1] = copy(current);
  else points[used++] = copy(current);
  return used;
}
} // namespace plotpng
