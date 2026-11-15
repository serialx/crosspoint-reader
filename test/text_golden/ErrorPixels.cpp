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

extern "C" void xp_app_test_reader_error(const XpUiCommand*, XpUiRect, const uint8_t*, size_t);
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
  size_t test = 0;
  // Exact shipped English, Hebrew and Arabic messages; Korean samples cover fallback
  // rendering because this revision has no Korean translation catalog.
  const std::array<std::string, 12> messages{
      "Page load error",     "Empty chapter", "Out of bounds", "שגיאה בטעינת הדף", "פרק ריק", "מחוץ לטווח",
      "خطأ في تحميل الصفحة", "فصل فارغ",      "خارج النطاق",   "페이지 로드 오류", "빈 장",   "범위를 벗어남"};
  for (int orientation = 0; orientation < 4; ++orientation)
    for (bool inverted : {false, true})
      for (bool scan : {false, true})
        for (int clipping = 0; clipping < 5; ++clipping)
          for (const auto& text : messages) {
            native.orient(orientation);
            rust.orient(orientation);
            native.display.inverted = rust.display.inverted = inverted;
            const auto width = native.renderer.getScreenWidth();
            const auto height = native.renderer.getScreenHeight();
            const std::array<std::array<int, 4>, 5> clips{{{0, 0, width, height},
                                                           {width / 2, 305, 20, 8},
                                                           {0, 0, width, 0},
                                                           {0, 330, width, 20},
                                                           {-20, -10, width + 40, height + 20}}};
            const auto before = clips[clipping];
            native.renderer.setClipRect(before[0], before[1], before[2], before[3]);
            rust.renderer.setClipRect(before[0], before[1], before[2], before[3]);
            auto exercise = [&] {
              ++test;
              native.display.pixels.fill(0x69);
              rust.display.pixels.fill(0x69);
              XpUiCommand command{};
              command.font = UI_12_FONT_ID;
              command.style = 1;
              command.kind = 0;
              command.rect.x =
                  (width - native.renderer.getTextWidth(UI_12_FONT_ID, text.c_str(), EpdFontFamily::BOLD)) / 2;
              command.rect.y = 300;
              XpUiRect clip{0, 0, static_cast<int16_t>(width), static_cast<int16_t>(height)};
              native.renderer.drawUiText(command, clip, text.c_str());
              target = &rust.renderer;
              xp_app_test_reader_error(&command, clip, reinterpret_cast<const uint8_t*>(text.data()), text.size());
              compare(native.display.pixels, rust.display.pixels, "reader error", test);
              assert(native.renderer.getClipRect() == rust.renderer.getClipRect());
              const auto restored = rust.renderer.getClipRect();
              assert(std::equal(restored.begin(), restored.end(), before.begin()));
              assert(native.renderer.getOrientation() == rust.renderer.getOrientation());
            };
            if (scan) {
              auto ns = native.cache.createPrewarmScope();
              auto rs = rust.cache.createPrewarmScope();
              exercise();
              ns.endScanAndPrewarm();
              rs.endScanAndPrewarm();
              exercise();
            } else {
              exercise();
            }
          }
  std::cout << "reader error cases=" << test << " exact comparisons=" << comparisons << "\n";
}
