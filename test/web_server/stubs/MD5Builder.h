#pragma once
#include "freeink-sdk/libs/font/FreeInkFont/third_party/freetype/src/base/md5.h"

class MD5Builder {
  MD5_CTX ctx{};
  unsigned char bytes[16]{};

 public:
  void begin() { MD5_Init(&ctx); }
  void add(const uint8_t* p, size_t n) { MD5_Update(&ctx, p, n); }
  void calculate() { MD5_Final(bytes, &ctx); }
  void getChars(char* out) {
    constexpr char digits[] = "0123456789abcdef";
    for (uint8_t byte : bytes) {
      *out++ = digits[byte >> 4];
      *out++ = digits[byte & 15];
    }
    *out = 0;
  }
};
