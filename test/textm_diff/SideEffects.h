#pragma once
#include "Callback.h"
#include "Fixtures.h"
// Independent SD owners prevent a C++ lookup from warming the Rust candidate's ring.
inline void checkSdEffects(const std::vector<std::string>& strings) {
  for (int warm = 0; warm < 3; ++warm) {
    SdCardFont cpp, rust;
    require(cpp.load("fixture.cpfont") && rust.load("fixture.cpfont"), "independent SD load");
    if (warm) {
      cpp.buildAdvanceTable("AV fi");
      rust.buildAdvanceTable("AV fi");
    }
    if (warm == 2) {
      cpp.prewarm("office AV fi", 15, false, true);
      rust.prewarm("office AV fi", 15, false, true);
    }
    GfxRenderer renderer;
    renderer.fontMap.emplace(3,
                             EpdFontFamily(cpp.getEpdFont(0), cpp.getEpdFont(1), cpp.getEpdFont(2), cpp.getEpdFont(3)));
    renderer.sdCardFonts_.emplace(3, &cpp);
    EpdFontFamily family(rust.getEpdFont(0), rust.getEpdFont(1), rust.getEpdFont(2), rust.getEpdFont(3));
    Callback callback{&family, &rust};
    auto face = callback.face();
    for (const auto& logical : strings) {
      std::string buffer;
      const char* visual = prepareVisualText(logical.c_str(), buffer, BidiUtils::BidiBaseDir::AUTO);
      auto cps = codepoints(visual);
      for (int style : {0, 3, 16, 32, 128, 131, 144})
        for (int layout : {0, 1}) {
          size_t before = sdFontTestReads;
          int a = renderer.getTextAdvanceX(
              3, visual, static_cast<EpdFontFamily::Style>(style), 2, BidiUtils::BidiBaseDir::LTR,
              layout ? GfxRenderer::TextMeasureMode::Layout : GfxRenderer::TextMeasureMode::Render);
          size_t cppReads = sdFontTestReads - before;
          before = sdFontTestReads;
          int b = xp_textm_advance(&face, cps.data(), cps.size(), style, 2, layout);
          require(a == b, "independent SD advance");
          require(sdFontTestReads - before == cppReads, "SD advance read count");
          before = sdFontTestReads;
          a = renderer.getTextWidth(3, visual, static_cast<EpdFontFamily::Style>(style), BidiUtils::BidiBaseDir::LTR);
          cppReads = sdFontTestReads - before;
          before = sdFontTestReads;
          b = xp_textm_width(&face, cps.data(), cps.size(), style);
          require(a == b, "independent SD width");
          require(sdFontTestReads - before == cppReads, "SD width read count");
        }
    }
  }
}
