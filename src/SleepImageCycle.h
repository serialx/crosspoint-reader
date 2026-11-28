#pragma once

#include <cstdint>

struct SleepImageCycle {
  uint32_t count = 0;  // High 16 bits: indexes left. Low 16 bits: total.
  uint32_t state = 0;  // High 16 bits: seed. Low 16 bits: last index.

  uint16_t next(uint16_t fileCount);  // fileCount must be nonzero.
};
