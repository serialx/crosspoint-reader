#pragma once
#include <cstddef>
#include <cstdint>
inline constexpr uint8_t BUILTIN_READER_POINT_SIZES[] = {12, 14, 16, 18};
inline uint8_t snapToNearestPointSize(const uint8_t* sizes, size_t count, uint8_t point) {
  if (!sizes || count == 0) return point;
  uint8_t best = sizes[0];
  uint8_t delta = best > point ? best - point : point - best;
  for (size_t i = 1; i < count; ++i) {
    const uint8_t d = sizes[i] > point ? sizes[i] - point : point - sizes[i];
    if (d < delta) {
      best = sizes[i];
      delta = d;
    }
  }
  return best;
}
