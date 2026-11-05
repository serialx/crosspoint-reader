#pragma once
#include <EpdFontFamily.h>

#include <cstdio>
#include <map>
class SdCardFont;
class GfxRenderer {
  std::map<int, EpdFontFamily> fonts;

 public:
  const auto& getFontMap() const { return fonts; }
  void registerSdCardFont(int id, SdCardFont*) { std::printf("register %d\n", id); }
  void insertFont(int id, const EpdFontFamily& family) {
    std::printf("insert %d\n", id);
    fonts.emplace(id, family);
  }
  void removeFont(int id) {
    std::printf("remove %d\n", id);
    fonts.erase(id);
  }
  void clearFallbackFonts() { std::puts("clear fallbacks"); }
  void clearSdCardFonts() { std::puts("clear sd"); }
};
