#include <BoardConfig.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <builtinFonts/all.h>
#include <components/icons/bookmark.h>
#include <fontIds.h>
#include <xp_app.h>
#include <xp_page_paint.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include "Trace.h"
static GfxRenderer* target;
#include "popup_native.inc"
extern "C" void xp_app_test_popup(const XpAppPopupFacts*, XpAppText, XpUiRect*, int32_t, uint8_t);
extern "C" uint8_t xp_native_render_target(XpPageTarget* out, XpUiPaint*) {
  target->pageTarget(*out, false);
  return target->getFontCacheManager() != nullptr;
}
void textGoldenGlyph(const EpdFontFamily&, uint32_t, int, int, EpdFontFamily::Style) {}
void textGoldenScaled(int, uint32_t, int, int, EpdFontFamily::Style) {}
struct Side {
  HalDisplay display;
  GfxRenderer renderer{display};
  FontCacheManager cache{renderer.getFontMap()};
  SdCardFont sd;
  Side(const char* font, bool primary) {
    renderer.begin();
    static EpdFont small(&ubuntu_12_regular), bold(&ubuntu_12_bold), estimate(&ubuntu_10_regular);
    renderer.insertFont(UI_12_FONT_ID, EpdFontFamily(&small, &bold));
    renderer.insertFont(UI_10_FONT_ID, EpdFontFamily(&estimate));
    renderer.setFontCacheManager(&cache);
    if (font) {
      assert(sd.load(font));
      renderer.insertFont(-99, EpdFontFamily(sd.getEpdFont(0), sd.getEpdFont(1), sd.getEpdFont(2), sd.getEpdFont(3)));
      renderer.registerSdCardFont(-99, &sd);
      renderer.setFallbackFont(UI_12_FONT_ID, -99);
      if (primary) {
        renderer.insertFont(UI_12_FONT_ID,
                            EpdFontFamily(sd.getEpdFont(0), sd.getEpdFont(1), sd.getEpdFont(2), sd.getEpdFont(3)));
        renderer.registerSdCardFont(UI_12_FONT_ID, &sd);
      }
    }
  }
  void orient(int orientation) {
    renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
    renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
  }
};
static size_t comparisons = 0;
static void compare(const std::array<uint8_t, HalDisplay::BUFFER_SIZE>& expected,
                    const std::array<uint8_t, HalDisplay::BUFFER_SIZE>& actual, const char* phase, size_t test) {
  if (expected != actual) {
    auto i = std::mismatch(expected.begin(), expected.end(), actual.begin()).first - expected.begin();
    std::cerr << "case " << test << " " << phase << " first byte " << i << " expected " << int(expected[i])
              << " actual " << int(actual[i]) << "\n";
    std::abort();
  }
  ++comparisons;
}
int main(int argc, char** argv) {
  Side native(argc > 1 ? argv[1] : nullptr, argc > 2), rust(argc > 1 ? argv[1] : nullptr, argc > 2);
  BaseTheme painter;
  size_t test = 0;
  const std::array<std::string, 7> messages{
      "Loading...",         "", " ", "한글 읽기", "עברית العربية", "A long loading message that passes the screen edge",
      std::string(160, 'W')};
  // Lyra3Covers inherits Lyra's popup metrics.
  for (const auto& metrics : {baseMetrics, lyraMetrics, lyraMetrics, roundedMetrics})
    for (int orientation = 0; orientation < 4; ++orientation)
      for (bool inverted : {false, true})
        for (bool scan : {false, true})
          for (const auto& text : messages) {
            GUI.metrics = metrics;
            native.orient(orientation);
            rust.orient(orientation);
            native.display.inverted = rust.display.inverted = inverted;
            auto exercise = [&] {
              ++test;
              native.display.pixels.fill(0x69);
              rust.display.pixels.fill(0x69);
              auto expected = painter.drawPopup(native.renderer, text.c_str());
              target = &rust.renderer;
              XpAppPopupFacts facts{};
              readFacts(&facts);
              XpAppText message{reinterpret_cast<const uint8_t*>(text.data()), text.size()};
              XpUiRect actual{};
              xp_app_test_popup(&facts, message, &actual, 0, 0);
              assert(expected.x == actual.x && expected.y == actual.y && expected.width == actual.w &&
                     expected.height == actual.h);
              compare(native.display.pixels, rust.display.pixels, "popup", test);
              for (int value : {-20, 0, 1, 50, 99, 100, 140}) {
                painter.fillPopupProgress(native.renderer, expected, value);
                xp_app_test_popup(&facts, message, &actual, value, 1);
                compare(native.display.pixels, rust.display.pixels, "progress", test);
              }
              // Invalid/empty progress geometry still performs no pixel writes.
              for (int width : {-1, 0, metrics.popupMarginX * 2}) {
                Rect empty{5, 6, width, 0};
                XpUiRect er{5, 6, static_cast<int16_t>(width), 0};
                painter.fillPopupProgress(native.renderer, empty, 50);
                xp_app_test_popup(&facts, message, &er, 50, 1);
                compare(native.display.pixels, rust.display.pixels, "empty progress", test);
              }
            };
            if (scan) {
              auto ns = native.cache.createPrewarmScope();
              auto rs = rust.cache.createPrewarmScope();
              exercise();
              ns.endScanAndPrewarm();
              rs.endScanAndPrewarm();
              exercise();
            } else
              exercise();
          }
  std::cout << "popup cases=" << test << " exact comparisons=" << comparisons << "\n";
}
