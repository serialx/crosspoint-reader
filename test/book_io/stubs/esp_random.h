#pragma once
#include <HalStorage.h>
inline void esp_fill_random(void* data, size_t len) {
  booktest::step("random", "", len);
  auto* bytes = static_cast<uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) bytes[i] = static_cast<uint8_t>(i * 13);
}
