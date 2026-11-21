#pragma once
#include "Trace.h"

#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
using md5_context_t = CC_MD5_CTX;
inline constexpr auto test_md5_init = CC_MD5_Init;
inline constexpr auto test_md5_update = CC_MD5_Update;
inline constexpr auto test_md5_final = CC_MD5_Final;
#else
#include <openssl/md5.h>
using md5_context_t = MD5_CTX;
inline constexpr auto test_md5_init = MD5_Init;
inline constexpr auto test_md5_update = MD5_Update;
inline constexpr auto test_md5_final = MD5_Final;
#endif

inline void esp_rom_md5_init(md5_context_t* context) {
  document_test::event("digest.begin");
  test_md5_init(context);
}
inline void esp_rom_md5_update(md5_context_t* context, const void* bytes, uint32_t count) {
  document_test::event("digest.add " + std::to_string(count));
  test_md5_update(context, bytes, count);
}
inline void esp_rom_md5_final(uint8_t* out, md5_context_t* context) {
  document_test::event("digest.finish");
  test_md5_final(out, context);
}
