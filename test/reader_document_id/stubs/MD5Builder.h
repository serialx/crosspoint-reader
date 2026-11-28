#pragma once
#include <cstring>

#include "esp_rom_md5.h"
class MD5Builder {
  md5_context_t context;
  uint8_t bytes[16];

 public:
  void begin() { esp_rom_md5_init(&context); }
  void add(const char* p) { add(reinterpret_cast<const uint8_t*>(p), strlen(p)); }
  void add(const uint8_t* p, size_t n) {
    assert(n <= 1024);
    esp_rom_md5_update(&context, p, uint32_t(n));
  }
  void calculate() { esp_rom_md5_final(bytes, &context); }
  std::string toString() {
    std::string s;
    for (auto b : bytes) {
      s += "0123456789abcdef"[b >> 4];
      s += "0123456789abcdef"[b & 15];
    }
    return s;
  }
};
