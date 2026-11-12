#pragma once
#include <Crypto.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include <cstdlib>
namespace freeink::content {
class WolfsslCrypto : public Crypto {
  static bool decrypt(const uint8_t* key, const uint8_t* iv, const uint8_t* in, size_t length, uint8_t* out,
                      const EVP_CIPHER* cipher) {
    auto* ctx = EVP_CIPHER_CTX_new();
    int n = 0, tail = 0;
    bool ok = EVP_DecryptInit_ex(ctx, cipher, nullptr, key, iv) == 1;
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    ok = ok && EVP_DecryptUpdate(ctx, out, &n, in, length) == 1 && EVP_DecryptFinal_ex(ctx, out + n, &tail) == 1;
    EVP_CIPHER_CTX_free(ctx);
    return ok;
  }

 public:
  bool aes256CbcDecrypt(const uint8_t key[32], const uint8_t iv[16], const uint8_t* in, size_t len,
                        uint8_t* out) override {
    return decrypt(key, iv, in, len, out, EVP_aes_256_cbc());
  }
  int32_t rsaPrivateRaw(const uint8_t* pkcs8Der, size_t pkcs8Len, const uint8_t* in, size_t inLen, uint8_t* out,
                        size_t outCap) override {
    std::abort();
  }
  bool aes128CbcDecrypt(const uint8_t key[16], const uint8_t iv[16], const uint8_t* in, size_t len,
                        uint8_t* out) override {
    return decrypt(key, iv, in, len, out, EVP_aes_128_cbc());
  }
  void sha1(const uint8_t* data, size_t len, uint8_t out[20]) override { SHA1(data, len, out); }
  void sha256(const uint8_t* data, size_t len, uint8_t out[32]) override { SHA256(data, len, out); }
  bool rsaGenerate(RsaKeyPairDer* out) override { std::abort(); }
  bool rsaPublicEncrypt(const uint8_t* certDer, size_t certLen, const uint8_t* in, size_t inLen, uint8_t* out,
                        size_t outCap, size_t* outLen) override {
    std::abort();
  }
  bool rsaPrivateSignRaw(const uint8_t* pkcs8Der, size_t pkcs8Len, const uint8_t hash[20], uint8_t out[128]) override {
    std::abort();
  }
  bool aes128CbcEncrypt(const uint8_t key[16], const uint8_t iv[16], const uint8_t* in, size_t len,
                        uint8_t* out) override {
    std::abort();
  }
  bool pkcs12Extract(const uint8_t* p12, size_t len, const std::string& password, std::vector<uint8_t>* keyPkcs8,
                     std::vector<uint8_t>* certDer) override {
    std::abort();
  }
  void randomBytes(uint8_t* out, size_t len) override { std::abort(); }
};
}  // namespace freeink::content
