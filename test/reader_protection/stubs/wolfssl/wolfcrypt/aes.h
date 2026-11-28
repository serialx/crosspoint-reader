#pragma once
#include <HalStorage.h>
#include <openssl/evp.h>

#include <cstring>
constexpr int INVALID_DEVID = -2;
struct Aes {
  uint8_t key[32];
};
inline int wc_AesInit(Aes*, void*, int) { return protection_test::step("aes-init") ? 0 : -1; }
inline void wc_AesFree(Aes*) { protection_test::trace.push_back("aes-free"); }
inline int wc_AesGcmSetKey(Aes* aes, const uint8_t* key, size_t length) {
  assert(length == 32);
  if (!protection_test::step("aes-key")) return -1;
  memcpy(aes->key, key, 32);
  return 0;
}
inline int gcm(Aes* aes, uint8_t* out, const uint8_t* in, size_t length, const uint8_t* iv, size_t ivLength,
               uint8_t* tag, size_t tagLength, const uint8_t* aad, size_t aadLength, bool encrypt) {
  if (!protection_test::step(encrypt ? "encrypt" : "decrypt")) return -1;
  auto* ctx = EVP_CIPHER_CTX_new();
  assert(ctx);
  int n = 0, tail = 0;
  bool ok = EVP_CipherInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr, encrypt) == 1;
  ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, ivLength, nullptr) == 1;
  ok = ok && EVP_CipherInit_ex(ctx, nullptr, nullptr, aes->key, iv, encrypt) == 1;
  ok = ok && EVP_CipherUpdate(ctx, nullptr, &n, aad, aadLength) == 1;
  ok = ok && EVP_CipherUpdate(ctx, out, &n, in, length) == 1;
  if (!encrypt) ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tagLength, tag) == 1;
  ok = ok && EVP_CipherFinal_ex(ctx, out + n, &tail) == 1;
  if (encrypt) ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tagLength, tag) == 1;
  EVP_CIPHER_CTX_free(ctx);
  return ok ? 0 : -1;
}
inline int wc_AesGcmEncrypt(Aes* a, uint8_t* o, const uint8_t* i, size_t n, const uint8_t* iv, size_t il, uint8_t* t,
                            size_t tl, const uint8_t* ad, size_t al) {
  return gcm(a, o, i, n, iv, il, t, tl, ad, al, true);
}
inline int wc_AesGcmDecrypt(Aes* a, uint8_t* o, const uint8_t* i, size_t n, const uint8_t* iv, size_t il,
                            const uint8_t* t, size_t tl, const uint8_t* ad, size_t al) {
  return gcm(a, o, i, n, iv, il, const_cast<uint8_t*>(t), tl, ad, al, false);
}
