#pragma once
#include <HalStorage.h>
#include <openssl/sha.h>
inline int wc_Sha256Hash(const uint8_t* input, size_t length, uint8_t* out) {
  if (!protection_test::step("sha256")) return -1;
  SHA256(input, length, out);
  return 0;
}
