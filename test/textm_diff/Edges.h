#pragma once
#include "Callback.h"
#include "Fixtures.h"
inline void checkEdges() {
  const EpdGlyph glyphs[] = {{3, 9, 15, -4, 8, 0, 0}, {5, 7, 0, -1, 9, 0, 0}};
  const EpdUnicodeInterval intervals[] = {{'A', 'A', 0}, {'M', 'M', 1}};
  for (int ascender : {-1, 0, 1, 255, 400})
    for (int count : {0, 1, 2}) {
      EpdFontData data{};
      data.glyph = glyphs;
      data.intervals = intervals;
      data.intervalCount = count;
      data.ascender = ascender;
      data.advanceY = 23;
      EpdFont font(&data);
      EpdFontFamily family(&font);
      GfxRenderer renderer;
      renderer.fontMap.emplace(1, family);
      Callback callback{&family, nullptr};
      auto face = callback.face();
      for (const char* text : {"", "A", "AZA", "A\u0301ZA", "█■A", "■█", "\u0301A", "A\u0001A"}) {
        auto cps = codepoints(text);
        for (int bits = 0; bits < 256; ++bits) {
          const auto style = static_cast<EpdFontFamily::Style>(bits);
          require(renderer.getTextWidth(1, text, style, BidiUtils::BidiBaseDir::LTR) ==
                      xp_textm_width(&face, cps.data(), cps.size(), bits),
                  "missing glyph / solid bounds");
          require(renderer.getTextAdvanceX(1, text, style, -3, BidiUtils::BidiBaseDir::LTR,
                                           GfxRenderer::TextMeasureMode::Render) ==
                      xp_textm_advance(&face, cps.data(), cps.size(), bits, -3, 0),
                  "missing glyph / solid advance");
        }
      }
    }
  require(xp_textm_advance(nullptr, nullptr, 0, 0, 0, 0) == 0, "null face empty stream");
  XpTextmGlyph wire{};
  static_assert(sizeof(wire) == 8);
}
