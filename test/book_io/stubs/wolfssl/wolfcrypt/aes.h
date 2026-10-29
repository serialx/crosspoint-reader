#pragma once
#include <HalStorage.h>
constexpr int INVALID_DEVID = -2;
struct Aes { uint8_t key[32]; };
inline int wc_AesInit(Aes*, void*, int) { return booktest::step("aes-init", "") ? 0 : -1; }
inline void wc_AesFree(Aes*) { booktest::step("aes-free", ""); }
inline int wc_AesGcmSetKey(Aes* aes, const uint8_t* key, size_t len) {
  if (!booktest::step("aes-key", "", len)) return -1;
  std::memcpy(aes->key, key, len);
  return 0;
}
inline void fakeTag(const uint8_t* bytes, size_t len, const uint8_t* aad, size_t aadLen, uint8_t* tag) {
  std::memset(tag, 0, 16);
  for (size_t i = 0; i < len; ++i) tag[i % 16] ^= bytes[i];
  for (size_t i = 0; i < aadLen; ++i) tag[i % 16] ^= aad[i];
}
inline int wc_AesGcmEncrypt(Aes* aes, uint8_t* out, const uint8_t* input, size_t len,
    const uint8_t* iv, size_t ivLen, uint8_t* tag, size_t, const uint8_t* aad, size_t aadLen) {
  if (!booktest::step("encrypt", "", len)) return -1;
  for (size_t i = 0; i < len; ++i) out[i] = input[i] ^ aes->key[i] ^ iv[i % ivLen];
  fakeTag(out, len, aad, aadLen, tag);
  return 0;
}
inline int wc_AesGcmDecrypt(Aes* aes, uint8_t* out, const uint8_t* input, size_t len,
    const uint8_t* iv, size_t ivLen, const uint8_t* tag, size_t, const uint8_t* aad, size_t aadLen) {
  if (!booktest::step("decrypt", "", len)) return -1;
  uint8_t expected[16]; fakeTag(input, len, aad, aadLen, expected);
  if (std::memcmp(tag, expected, 16)) return -1;
  for (size_t i = 0; i < len; ++i) out[i] = input[i] ^ aes->key[i] ^ iv[i % ivLen];
  return 0;
}
