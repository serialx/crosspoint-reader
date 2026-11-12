#pragma once
#include <cstddef>
#include <cstdint>
inline void esp_fill_random(void* data, size_t size) {
  auto* bytes = static_cast<uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) bytes[i] = uint8_t(i + 23);
}
