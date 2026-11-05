#pragma once
#include <HalStorage.h>
inline bool deviceSecret(uint8_t (&out)[32]) {
  if (!booktest::step("secret", "")) return false;
  for (size_t i = 0; i < 32; ++i) out[i] = static_cast<uint8_t>(i * 7);
  return true;
}
