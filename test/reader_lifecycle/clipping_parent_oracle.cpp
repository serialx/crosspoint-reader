#include <xp_ui_reader.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "clippings/ClippingText.h"
static int modelDepth = 0;
static void event(const char* name, int a = 0, int b = 0) {
  assert(modelDepth == 0);
  std::printf("%s %d %d\n", name, a, b);
}
static unsigned long clockMs = 100;
unsigned long millis() {
  event("clock", clockMs);
  return clockMs++;
}
#ifdef CLIPPING_RELEASE_LOG
#define LOG_DBG(...) ((void)0)
#else
#define LOG_DBG(tag, fmt, ...) (assert(modelDepth == 0), std::printf("debug " fmt "\n", ##__VA_ARGS__))
#endif
struct Rect {
  int x, y, width, height;
};
enum class Color { LightGray };
struct BoardConfig {
  static inline bool a4 = false;
  static bool isEegoA4() {
    event("a4", a4);
    return a4;
  }
};
struct EpdFontFamily {
  enum Style { Normal = 0, UNDERLINE = 4 };
};
struct GfxRenderer {
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  RenderMode mode = BW;
  RenderMode getRenderMode() const {
    event("mode", mode);
    return mode;
  }
  void getOrientedViewableTRBL(int* t, int* r, int* b, int* l) const {
    event("margins");
    *t = 1;
    *r = 2;
    *b = 3;
    *l = 4;
  }
  int getTextAdvanceX(int font, const char* text, EpdFontFamily::Style style, int spacing = 0) const {
    assert(modelDepth == 0);
    std::printf("advance %d [%s] %d %d\n", font, text, int(style), spacing);
    if (std::strcmp(text, "zero") == 0) return 0;
    if (std::strcmp(text, "negative") == 0) return -1;
    if (std::strcmp(text, "\xe2\x80\x83") == 0) return 6;
    return int(std::strlen(text)) * 3 + font % 3 + spacing;
  }
  int getFontAscenderSize(int font) const {
    event("ascender", font);
    return 9 + font % 3;
  }
  int getLineHeight(int font) const {
    event("height", font);
    return 10 + font % 3;
  }
  void drawPixel(int x, int y, bool color) const {
    assert(modelDepth == 0);
    std::printf("pixel %d %d %d\n", x, y, color);
  }
  void fillRectDither(int x, int y, int w, int h, Color) const {
    assert(modelDepth == 0);
    std::printf("dither %d %d %d %d\n", x, y, w, h);
  }
};
struct Word {
  const char* text;
  int x, font, style, y;
  uint32_t start, end;
};
struct TextBlock {
  struct SourceRange {
    uint32_t start, end;
  };
  struct Style {
    int characterSpacing = 1;
  };
  TextBlock(const std::vector<Word>* words, bool valid = true, int font = 8)
      : words(words), validValue(valid), font(font) {}
  TextBlock(const TextBlock&) = delete;
  const std::vector<Word>* words;
  bool validValue = true;
  int font = 8;
  bool valid() const { return validValue; }
  uint16_t wordCount() const { return words->size(); }
  int wordFontId(uint16_t i, int fallback) const {
    event("word-font", i, fallback);
    return (*words)[i].font;
  }
  static int renderFontId(const GfxRenderer&, int font, int fallback) {
    event("render-font", font, fallback);
    return font == 42 ? fallback : font;
  }
  int blockFontId(int fallback) const {
    event("block-font", fallback);
    return font;
  }
  const char* wordText(uint16_t i) const { return (*words)[i].text; }
  int wordStyle(uint16_t i) const { return (*words)[i].style; }
  int wordXpos(uint16_t i) const { return (*words)[i].x; }
  Style getBlockStyle() const { return {}; }
  int getRubyShift(int asc) const {
    event("ruby", asc);
    return asc / 3;
  }
  int wordYOffset(const GfxRenderer&, int base, uint16_t i) const {
    event("y-offset", base, i);
    return (*words)[i].y;
  }
  SourceRange wordSourceRange(uint16_t i) const { return {(*words)[i].start, (*words)[i].end}; }
};
constexpr int TAG_PageLine = 3;
struct PageElementView {
  int tag, xPos, yPos;
  struct BlockSeed {
    const std::vector<Word>* words;
    bool validValue = true;
    int font = 8;
  } block;
  int getTag() const { return tag; }
  TextBlock textBlock() const { return TextBlock{block.words, block.validValue, block.font}; }
};
struct Page {
  std::vector<PageElementView> lines;
  uint16_t elementCount() const { return lines.size(); }
  PageElementView element(uint16_t i) const {
    event("element", i);
    return lines[i];
  }
};
struct Settings {
  int screenMargin = 2;
  int getReaderFontId() const {
    event("reader-font");
    return 6;
  }
  uint32_t readerRenderSpec(int w, int h) const {
    event("spec", w, h);
    return 123;
  }
} SETTINGS;
uint32_t readerRenderSpecSignature(uint32_t spec) {
  event("signature", spec);
  return spec;
}
// CLIPPING_RECORD
// CLIPPING_MATCH
struct Clippings {
  std::vector<Clipping> entries;
  bool hasClippings() const {
    event("has-clips", !entries.empty());
    return !entries.empty();
  }
  size_t clippingCount() const { return entries.size(); }
  const Clipping* clippingAt(size_t i) const {
    event("clip", i);
    return &entries[i];
  }
} CLIPPINGS;
// GEOMETRY_METHODS
struct Section {
  int currentPage = 1;
  uint16_t pageCount = 3;
};
struct EpubReaderActivity {
  struct ModelLock {
    explicit ModelLock(EpubReaderActivity&) {
      assert(modelDepth == 0);
      ++modelDepth;
    }
    ~ModelLock() { --modelDepth; }
  };
  GfxRenderer renderer;
  std::unique_ptr<Section> section = std::make_unique<Section>();
  int currentSpineIndex = 2, buildViewportWidth = 80, buildViewportHeight = 100;
  int runClippingPolicy(uint8_t operation, const Page& page, const XpUiReaderClipInput& input) const;
  int clippingAtPoint(const Page&, int, int) const;
  void drawClippingHighlights(const Page&, int, int, int) const;
};
// CLIPPING_METHODS
int main() {
  const std::vector<Word> ltr = {{"one", 0, 6, 0, 0, 0, 3},        {"   ", 11, 6, 0, 0, 3, 6},
                                 {"zero", 13, 6, 0, 0, 6, 10},     {"\xe2\x80\x83two", 18, 9, 4, 2, 10, 16},
                                 {"three", 23, 42, 5, -1, 16, 21}, {"negative", 40, 6, 0, 0, 21, 29},
                                 {"last", 45, 6, 0, 0, 29, 33}};
  const std::vector<Word> rtl = {{"right", 50, 6, 4, 0, 33, 38},
                                 {"middle", 25, 6, 0, 0, 38, 44},
                                 {"left", 0, 9, 0, 1, 44, 48},
                                 {"", 0, 6, 0, 0, 48, 48}};
  Page page{{{0, 0, 0, {&ltr}},
             {TAG_PageLine, 2, 3, {&ltr}},
             {TAG_PageLine, 1, 20, {&ltr, false}},
             {TAG_PageLine, -3, 30, {&rtl}}}};
  for (bool a4 : {false, true})
    for (auto mode : {GfxRenderer::BW, GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB})
      for (int current : {-1, 0, 1, 3})
        for (int set = 0; set < 6; ++set) {
          std::printf("case %d %d %d %d\n", a4, mode, current, set);
          clockMs = 100;
          EpubReaderActivity o;
          o.renderer.mode = mode;
          o.section->currentPage = current;
          BoardConfig::a4 = a4;
          CLIPPINGS.entries.clear();
          if (set) {
            Clipping c;
            c.spineIndex = set == 1 ? 1 : 2;
            c.startPage = 0;
            c.endPage = 2;
            c.pageCount = 3;
            c.startWordIndex = 0;
            c.endWordIndex = 20;
            c.layoutSignature = set == 2 ? 0 : 123;
            if (set == 4) {
              c.startOffset = 3;
              c.endOffset = 40;
            }
            if (set == 5) {
              c.startWordIndex = 1;
              c.endWordIndex = 3;
              c.startPage = c.endPage = 1;
            }
            CLIPPINGS.entries.push_back(c);
            c.spineIndex = 1;
            CLIPPINGS.entries.push_back(c);
          }
          o.drawClippingHighlights(page, 6, 4, 7);
          if (current >= 0 && current < 3)
            for (int x : {-1, 8, 10, 22, 30, 50, 75})
              for (int y : {4, 10, 15, 38, 42, 50}) event("hit", o.clippingAtPoint(page, x, y));
        }
  EpubReaderActivity absent;
  absent.section.reset();
  absent.drawClippingHighlights(page, 6, 4, 7);
}
