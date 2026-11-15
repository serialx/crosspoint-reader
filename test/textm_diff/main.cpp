#include <array>
#include <iostream>

#include "Bench.h"
#include "Callback.h"
#include "Edges.h"
#include "Fixtures.h"
#include "LayoutCalls.h"
#include "Renderer.h"
#include "SideEffects.h"
using Style = EpdFontFamily::Style;
using Dir = BidiUtils::BidiBaseDir;
using Mode = GfxRenderer::TextMeasureMode;
uint64_t checksum = 14695981039346656037ULL;
size_t cases = 0;
void compare(int cpp, int rust, const char* what) {
  if (cpp != rust) {
    std::fprintf(stderr, "%s: C++ %d != Rust %d at case %zu\n", what, cpp, rust, cases);
    std::exit(1);
  }
}
void observe(int value) {
  for (int i = 0; i < 4; ++i) {
    checksum ^= static_cast<uint32_t>(value) >> (i * 8) & 255;
    checksum *= 1099511628211ULL;
  }
}
extern "C" size_t xp_test_allocation_attempts();
extern "C" void xp_test_fail_allocation_after(size_t);
int main(int argc, char** argv) {
  EpdFont regular(&notosans_12_regular), bold(&notosans_12_bold), italic(&notosans_12_italic),
      bi(&notosans_12_bolditalic), ubuntu(&ubuntu_12_regular);
  GfxRenderer renderer;
  renderer.fontMap.emplace(1, EpdFontFamily(&regular, &bold, &italic, &bi));
  renderer.fontMap.emplace(2, EpdFontFamily(&ubuntu, nullptr, &italic));
  if (argc > 1 && (std::strcmp(argv[1], "--bench-rust") == 0 || std::strcmp(argv[1], "--bench-cpp") == 0))
    return benchmark(std::strcmp(argv[1], "--bench-rust") == 0, renderer, argc > 2 ? std::atoi(argv[2]) : 1000000);
  if (argc > 1 && std::strcmp(argv[1], "--layout-count") == 0) return layoutCalls(renderer);
  makeSdFont();
  if (argc > 1 && std::strcmp(argv[1], "--corpus") != 0) {
    std::ifstream file(argv[1], std::ios::binary);
    require(file.good(), "private SD fixture open");
    sdFontTestFile.assign(std::istreambuf_iterator<char>(file), {});
  }
  SdCardFont sd;
  require(sd.load("fixture.cpfont"), "SD load");
  renderer.fontMap.emplace(3, EpdFontFamily(sd.getEpdFont(0), sd.getEpdFont(1), sd.getEpdFont(2), sd.getEpdFont(3)));
  renderer.sdCardFonts_.emplace(3, &sd);
  renderer.fallbackFontMap_.emplace(2, 3);
  const bool extended = argc > 2 && std::strcmp(argv[1], "--corpus") == 0;
  auto strings = corpus();
  if (argc > 2 && std::strcmp(argv[1], "--corpus") == 0) {
    strings.clear();
    std::ifstream input(argv[2]);
    require(input.good(), "corpus open");
    std::string hex;
    while (std::getline(input, hex)) {
      std::string word;
      for (size_t i = 0; i < hex.size(); i += 2)
        word.push_back(static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
      strings.push_back(word);
    }
  }
  xp_test_fail_allocation_after(0);
  auto reg = registry(renderer);
  for (int warm = 0; warm < 3; ++warm) {
    if (warm == 1) sd.buildAdvanceTable("AV fi \u0301\u0327\u2588");
    if (warm == 2)
      for (auto& s : strings) sd.buildAdvanceTable(s.c_str());
    for (int font : {1, 2, 3, 99})
      for (const auto& logical : strings)
        for (auto dir : {Dir::AUTO, Dir::LTR, Dir::RTL}) {
          if (extended && dir != Dir::AUTO) continue;
          std::string visualBuffer;
          const char* visual = prepareVisualText(logical.c_str(), visualBuffer, dir);
          auto logicalCps = codepoints(logical.c_str());
          auto visualCps = codepoints(visual);
          for (int bits = 0; bits < 256; ++bits) {
            if (extended && !((bits & ~3) == 0 || (bits & ~3) == 16 || (bits & ~3) == 32 || (bits & ~3) == 128))
              continue;
            auto style = static_cast<Style>(bits);
            int resolved = renderer.resolveTextFontId(font, logical.c_str(), style);
            observe(resolved);
            compare(resolved, xp_textm_resolve_font(&reg, font, logicalCps.data(), logicalCps.size(), bits), "resolve");
            int measuredFont = renderer.resolveTextFontId(resolved, visual, style);
            auto fit = renderer.fontMap.find(measuredFont);
            auto sit = renderer.sdCardFonts_.find(measuredFont);
            Callback callback{fit == renderer.fontMap.end() ? nullptr : &fit->second,
                              sit == renderer.sdCardFonts_.end() ? nullptr : sit->second};
            auto face = callback.face();
            const auto* f = callback.family ? &face : nullptr;
            auto original = renderer.fontMap.find(font);
            auto originalSd = renderer.sdCardFonts_.find(font);
            Callback raw{original == renderer.fontMap.end() ? nullptr : &original->second,
                         originalSd == renderer.sdCardFonts_.end() ? nullptr : originalSd->second};
            auto rawFace = raw.face();
            const auto* r = raw.family ? &rawFace : nullptr;
            auto width = renderer.getTextWidth(resolved, visual, style, Dir::LTR);
            observe(width);
            compare(width, xp_textm_width(f, visualCps.data(), visualCps.size(), bits), "width");
            observe(renderer.getSpaceWidth(font, style));
            compare(renderer.getSpaceWidth(font, style), xp_textm_space_width(r, bits), "space width");
            observe(renderer.getSpaceAdvance(font, 'A', 'V', style));
            compare(renderer.getSpaceAdvance(font, 'A', 'V', style), xp_textm_space_advance(r, 'A', 'V', bits),
                    "space advance");
            int32_t advance = -123;
            int top = -456;
            auto found = renderer.getCodepointMetrics(font, 0x2588, style, advance, top);
            observe(found);
            observe(advance);
            observe(top);
            int32_t ra = -123, rt = -456;
            compare(found, xp_textm_codepoint(r, 0x2588, bits, &ra, &rt), "codepoint status");
            compare(advance, ra, "codepoint advance");
            compare(top, rt, "codepoint top");
            for (int tracking : {-128, -3, 0, 2, 127})
              for (auto mode : {Mode::Layout, Mode::Render}) {
                if (extended && tracking != 0) continue;
                auto a = renderer.getTextAdvanceX(resolved, visual, style, tracking, Dir::LTR, mode);
                observe(a);
                compare(a,
                        xp_textm_advance(f, visualCps.data(), visualCps.size(), bits, tracking, mode == Mode::Layout),
                        "advance");
                observe(renderer.getKerning(font, 'A', 'V', style, tracking));
                compare(renderer.getKerning(font, 'A', 'V', style, tracking),
                        xp_textm_kerning(r, 'A', 'V', bits, tracking), "kern");
                ++cases;
              }
          }
        }
  }
  for (int font : {1, 2, 3, 99}) {
    observe(renderer.getFontAscenderSize(font));
    observe(renderer.getTextHeight(font));
    observe(renderer.getLineHeight(font));
    observe(renderer.getLineHeight(font, 0.85f));
  }
  for (int font : {1, 2, 3, 99}) {
    auto fit = renderer.fontMap.find(font);
    Callback callback{fit == renderer.fontMap.end() ? nullptr : &fit->second, nullptr};
    auto face = callback.face();
    const auto* f = callback.family ? &face : nullptr;
    compare(renderer.getFontAscenderSize(font), xp_textm_ascender(f), "ascender");
    compare(renderer.getTextHeight(font), xp_textm_ascender(f), "text height");
    for (float compression : {-0.5f, 0.0f, 0.85f, 1.0f, 1.25f})
      compare(renderer.getLineHeight(font, compression), xp_textm_line_height(f, compression), "line height");
  }
  if (!extended) checkSdEffects(strings);
  checkEdges();
  require(xp_test_allocation_attempts() == 0, "measurement allocated Rust storage");
  if (argc == 1) require(checksum == 0x0fb1ef2d6653c9b2ULL, "public baseline digest drift");
  std::cout << "cases=" << cases << " checksum=" << std::hex << checksum << '\n';
}
