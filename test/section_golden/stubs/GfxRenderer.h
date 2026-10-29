#pragma once

#include <Arduino.h>
#ifdef XP_FOOTNOTE_SELECT_TEST
#include <xp_ui.h>

#include <vector>
#endif
#include <FontCacheManager.h>

#include <algorithm>
#include <cstdlib>

#define GfxRenderer ChapterTestRenderer
#include "../../chapter_html_slim_parser/stubs/GfxRenderer.h"
#undef GfxRenderer

// Font ids are integer em sizes. No host font installation influences the oracle.
class GfxRenderer : public ChapterTestRenderer {
 public:
#ifdef XP_FOOTNOTE_SELECT_TEST
  void getOrientedViewableTRBL(int* top, int* right, int* bottom, int* left) { *top = *right = *bottom = *left = 0; }
  bool failFootnoteBuild = false, paintOk = true, snapshotOk = true;
  int highlightX = 0, highlightY = 0;
  std::string highlightNumber;
  void clearScreen() {}
  void drawUiText(const XpUiCommand&, XpUiRect, const char* text) { highlightNumber = text; }
  bool drawUiScene(const XpUiScene& scene, const XpUiPaint* paint) {
    if (!paintOk) return false;
    std::vector<uint8_t> bytes(48000, 0xff);
    XpGlyphTarget target{
        bytes.data(), static_cast<uint32_t>(bytes.size()), 480, 800, 60, 0, 800, {0, 0, 480, 800}, 3, 0, 0, 0, 0};
    return xp_ui_render(&scene, &target, paint);
  }
  void displayBuffer(int) {}
  size_t readFramebufferRegion(int x, int y, int, int, uint8_t*, size_t) {
    if (!snapshotOk) return 0;
    highlightX = x;
    highlightY = y;
    return 1;
  }
  void writeFramebufferRegion(int, int, int, int, const uint8_t*) {}
  using ChapterTestRenderer::drawText;
  void drawText(int, int, int, const char* text, bool) { highlightNumber = text; }
#endif
  GfxRenderer() {
    for (int em = 8; em <= 80; ++em) {
      fontMap.emplace(em, EpdFontFamily(nullptr));
      fontLineHeights[em] = em;
      fontAscenders[em] = em * 3 / 4;
    }
    setFontVariantProvider({nullptr, [](void*, int fontId, float scale) {
                              return std::clamp(static_cast<int>(fontId * scale + 0.5f), 8, 80);
                            }});
  }
  static int advance(int em, uint32_t cp) {
    if (utf8IsCombiningMark(cp)) return 0;
    if (cp == ' ' || cp == 0xA0) return std::max(1, em / 4);
    if (cp >= 0x2E80) return em;
    if (cp == 'i' || cp == 'l' || cp == '.' || cp == ',') return std::max(1, em / 4);
    return std::max(1, em / 2);
  }
  static int pairKerning(uint32_t left, uint32_t right) {
    if ((left == 'A' && (right == 'V' || right == 'W')) ||
        ((left == 'T' || left == 'V') && (right == 'a' || right == 'o')))
      return -1;
    return 0;
  }
  int getTextAdvanceX(int font, const char* text, EpdFontFamily::Style style, int8_t tracking = 0,
                      BidiUtils::BidiBaseDir = BidiUtils::BidiBaseDir::AUTO,
                      TextMeasureMode = TextMeasureMode::Layout) const {
    int width = 0;
    uint32_t previous = 0;
    const auto* cursor = reinterpret_cast<const uint8_t*>(text);
    while (uint32_t cp = utf8NextCodepoint(&cursor)) {
      if (utf8IsCombiningMark(cp)) continue;
      if (style & EpdFontFamily::SMALL_CAPS) cp = utf8SmallCapsUpper(cp);
      width += advance(font, cp) + pairKerning(previous, cp) + trackingBetween(previous, cp, tracking);
      previous = cp;
    }
    return width;
  }
  int getTextWidth(int font, const char* text, EpdFontFamily::Style style,
                   BidiUtils::BidiBaseDir dir = BidiUtils::BidiBaseDir::AUTO) const {
    return getTextAdvanceX(font, text, style, 0, dir);
  }
  int getKerning(int, uint32_t left, uint32_t right, EpdFontFamily::Style, int8_t tracking = 0) const {
    return pairKerning(left, right) + trackingBetween(left, right, tracking);
  }
  int getSpaceWidth(int font, EpdFontFamily::Style) const { return advance(font, ' '); }
  int getSpaceAdvance(int font, uint32_t, uint32_t, EpdFontFamily::Style) const { return advance(font, ' '); }
  bool getCodepointMetrics(int font, uint32_t cp, EpdFontFamily::Style, int32_t& fp, int& top) const {
    fp = advance(font, cp) * 16;
    top = getFontAscenderSize(font);
    return true;
  }
  FontCacheManager* getFontCacheManager() const { return nullptr; }

  // Needed to compile the real ImageBlock serializer; pixel rendering is outside this tool.
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  uint8_t* getWriteTarget() { std::exit(EXIT_FAILURE); }
  int getWriteOriginY() const { return 0; }
  int getWriteRows() const { return 0; }
  RenderMode getRenderMode() const { return BW; }
  bool grayPlanesAreAbsolute() const { return false; }
  int getDisplayWidthBytes() const { return 60; }
  int getDisplayWidth() const { return 480; }
  int getDisplayHeight() const { return 800; }
  Orientation getOrientation() const { return LandscapeCounterClockwise; }
  bool glyphIntersectsStrip(int, int, int, int) const { return false; }
  void preserveImagePolarity(int, int, int, int) {}
};

#ifdef XP_FOOTNOTE_SELECT_TEST
namespace HalDisplay {
constexpr int FAST_REFRESH = 0;
}
#endif
