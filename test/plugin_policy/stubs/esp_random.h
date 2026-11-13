#pragma once
#include <cstdint>
inline unsigned pluginRandomCalls = 0;
inline uint32_t esp_random() {
  ++pluginRandomCalls;
  return 0x3fa9c21b;
}
