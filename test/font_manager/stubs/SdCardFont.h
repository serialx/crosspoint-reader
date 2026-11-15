#pragma once
#include <EpdFont.h>

#include <cstdio>
#include <cstdlib>
#include <new>
class SdCardFont {
  EpdFontData data{};
  EpdFont font{&data};

 public:
  static inline bool failCreate = false, failLoad = false;
  static inline unsigned live = 0;
  static void* operator new(size_t bytes, const std::nothrow_t&) noexcept {
    std::printf("create %d\n", failCreate);
    return failCreate ? nullptr : std::malloc(bytes);
  }
  static void operator delete(void* p) noexcept { std::free(p); }
  SdCardFont() { ++live; }
  ~SdCardFont() {
    --live;
    std::puts("destroy");
  }
  bool load(const char* path) {
    std::printf("load %s %d\n", path, failLoad);
    return !failLoad;
  }
  uint32_t contentHash() const { return 0x12345678; }
  uint8_t styleCount() const { return 4; }
  EpdFont* getEpdFont(uint8_t) { return &font; }
};
