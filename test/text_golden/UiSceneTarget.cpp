#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <builtinFonts/all.h>
#include <fontIds.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

#include "Trace.h"
void textGoldenGlyph(const EpdFontFamily&, uint32_t, int, int, EpdFontFamily::Style) {}
void textGoldenScaled(int, uint32_t, int, int, EpdFontFamily::Style) {}

int main(int argc, char** argv) {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  static EpdFont regular(&ubuntu_10_regular), bold(&ubuntu_10_bold);
  renderer.insertFont(UI_10_FONT_ID, EpdFontFamily(&regular, &bold));
  FontDecompressor decompressor;
  assert(decompressor.init());
  FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
  SdCardFont sd;
  if (argc > 1) {
    assert(sd.load(argv[1]));
    renderer.insertFont(UI_10_FONT_ID,
                        EpdFontFamily(sd.getEpdFont(0), sd.getEpdFont(1), sd.getEpdFont(2), sd.getEpdFont(3)));
    renderer.registerSdCardFont(UI_10_FONT_ID, &sd);
  }
  struct Owner {
    GfxRenderer* renderer;
    const char* text;
  } owner{&renderer, "العربية — Été — Ελληνικά"};
  const XpUiPaint resources{&owner, nullptr, nullptr,
                            [](void*, uint32_t h, uint32_t g) -> uint8_t { return h == 1 && g == 1; },
                            [](void* p, const XpGlyphTarget*, const XpUiCommand* c, XpUiRect clip) {
                              auto& owner = *static_cast<Owner*>(p);
                              owner.renderer->drawUiText(*c, clip, owner.text);
                            }};
  for (const char* text : {"Résumé / עברית / Україна", "עברית", "العربية"}) {
    const char* segments[] = {text, owner.text};
    const size_t lengths[] = {strlen(text) + 1, strlen(owner.text) + 1};
    renderer.ensureSdCardFontReady(UI_10_FONT_ID, segments, lengths, 2, true, true, 15, true);
    for (int orientation = 0; orientation < 4; ++orientation) {
      renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
      renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
      XpUiCommand commands[4]{};
      commands[0].kind = XP_UI_CLEAR;
      commands[1].kind = XP_UI_FILL;
      commands[1].rect = {5, 7, 151, 63};
      commands[1].paint = 2;
      commands[2].kind = XP_UI_TEXT;
      commands[2].rect = {13, 34, 0, 0};
      commands[2].font = UI_10_FONT_ID;
      commands[2].text_len = strlen(text);
      commands[3].kind = XP_UI_TEXT_HANDLE;
      commands[3].rect = {9, 102, 320, 90};
      commands[3].font = UI_10_FONT_ID;
      commands[3].max_lines = 2;
      commands[3].resource = 1;
      commands[3].generation = 1;
      XpUiScene scene{commands, reinterpret_cast<const uint8_t*>(text),  nullptr, 1,
                      4,        static_cast<uint16_t>(strlen(text) + 1), 0,       XP_UI_NONE,
                      0};
      assert(renderer.drawUiScene(scene, &resources));
      const auto expected = display.pixels;
      display.pixels.fill(0x69);
      std::array<uint8_t, 48000> assembled{};
      std::array<uint8_t, 1602> band{};
      for (int row = 0; row < 480; row += 16) {
        band.fill(0xA5);
        XpGlyphTarget target{band.data() + 1,
                             1600,
                             800,
                             480,
                             100,
                             row,
                             16,
                             {0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()},
                             static_cast<uint8_t>(orientation),
                             0,
                             0,
                             1,
                             0};
        assert(renderer.drawUiScene(scene, target, &resources));
        assert(band.front() == 0xA5 && band.back() == 0xA5);
        assert(std::all_of(display.pixels.begin(), display.pixels.end(), [](uint8_t b) { return b == 0x69; }));
        std::copy_n(band.data() + 1, 1600, assembled.data() + row * 100);
      }
      assert(assembled == expected);
      // Message layout still agrees with the C++ placement for multilingual text.
      XpUiMetrics metrics{
          &renderer,
          [](void* p, int32_t font, const uint8_t* s, uint16_t, uint8_t) -> int32_t {
            return static_cast<GfxRenderer*>(p)->getTextWidth(font, reinterpret_cast<const char*>(s));
          },
          [](void* p, int32_t font) -> int32_t { return static_cast<GfxRenderer*>(p)->getLineHeight(font); }};
      XpUiRect screen{0, 0, static_cast<int16_t>(renderer.getScreenWidth()),
                      static_cast<int16_t>(renderer.getScreenHeight())};
      XpUiCommand message[2]{};
      assert(xp_ui_message_view(&metrics, screen, UI_10_FONT_ID, 0, reinterpret_cast<const uint8_t*>(text),
                                strlen(text), message, 2));
      XpUiScene messageScene{message, reinterpret_cast<const uint8_t*>(text),  nullptr, 0,
                             2,       static_cast<uint16_t>(strlen(text) + 1), 0,       XP_UI_NONE,
                             0};
      assert(renderer.drawUiScene(messageScene));
      const auto messagePixels = display.pixels;
      renderer.clearScreen();
      renderer.drawText(UI_10_FONT_ID, (screen.w - renderer.getTextWidth(UI_10_FONT_ID, text)) / 2,
                        (screen.h - renderer.getLineHeight(UI_10_FONT_ID)) / 2, text);
      assert(display.pixels == messagePixels);
    }
  }
  std::cout << "Mixed clear/geometry/text/resource scene: full frame == 16-row bands in all orientations; multilingual "
               "message parity passed\n";
}
