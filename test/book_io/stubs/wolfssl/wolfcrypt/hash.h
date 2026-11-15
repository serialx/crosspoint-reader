#pragma once
#include <HalStorage.h>
inline int wc_Sha256Hash(const uint8_t* input, size_t len, uint8_t* out) {
  if (!booktest::step("sha256", "", len)) return -1;
  std::memset(out, 0, 32);
  for (size_t i = 0; i < len; ++i) out[i % 32] ^= input[i];
  return 0;
}
