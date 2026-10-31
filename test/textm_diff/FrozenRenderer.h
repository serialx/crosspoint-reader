#pragma once
#include <BidiUtils.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <Utf8.h>

#include <map>
#include <string>

// Pre-T4 methods from extract.py, bound to the same real font owners as the native provider.
class FrozenRenderer {
 public:
  enum class TextMeasureMode { Layout, Render };
  std::map<int, EpdFontFamily> fontMap;
  std::map<int, int> fallbackFontMap_;
  std::map<int, SdCardFont*> sdCardFonts_;
  int resolveTextFontId(int font, const char* text, EpdFontFamily::Style style) const;
  int getTextWidth(int font, const char* text, EpdFontFamily::Style style, BidiUtils::BidiBaseDir dir) const;
  int getTextAdvanceX(int font, const char* text, EpdFontFamily::Style style, int8_t tracking,
                      BidiUtils::BidiBaseDir dir, TextMeasureMode mode) const;
  int getSpaceWidth(int font, EpdFontFamily::Style style) const;
  int getSpaceAdvance(int font, uint32_t left, uint32_t right, EpdFontFamily::Style style) const;
  int getKerning(int font, uint32_t left, uint32_t right, EpdFontFamily::Style style, int8_t tracking) const;
  bool getCodepointMetrics(int font, uint32_t cp, EpdFontFamily::Style style, int32_t& advance, int& top) const;
  int getFontAscenderSize(int font) const;
  int getLineHeight(int font) const;
  int getLineHeight(int font, float compression) const;
  int getTextHeight(int font) const;
  void ensureSdGlyphsResident(int font, const char* text, EpdFontFamily::Style style, bool) const {
    auto it = sdCardFonts_.find(font);
    if (it != sdCardFonts_.end()) it->second->prewarm(text, it->second->resolveStyleMask(1 << (style & 3)));
  }
};
