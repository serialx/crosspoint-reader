#pragma once
#include <CommonCrypto/CommonDigest.h>

#include "Trace.h"
using md5_context_t = CC_MD5_CTX;
inline void esp_rom_md5_init(md5_context_t* context) {
  document_test::event("digest.begin");
  CC_MD5_Init(context);
}
inline void esp_rom_md5_update(md5_context_t* context, const void* bytes, uint32_t count) {
  document_test::event("digest.add " + std::to_string(count));
  CC_MD5_Update(context, bytes, count);
}
inline void esp_rom_md5_final(uint8_t* out, md5_context_t* context) {
  document_test::event("digest.finish");
  CC_MD5_Final(out, context);
}
