#include <BoardConfig.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <builtinFonts/all.h>
#include <components/icons/bookmark.h>
#include <fontIds.h>
#include <xp_epub_session.h>
#include <xp_page_paint.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include "Trace.h"
#include "status_spec.inc"
struct Rect {
  int x, y, width, height;
};
struct ThemeMetrics {
  int batteryWidth = 15, batteryHeight = 12, statusBarHorizontalMargin = 5, statusBarVerticalMargin = 19,
      progressBarMarginTop = 1;
};
struct UITheme {
  ThemeMetrics metrics;
  static UITheme& getInstance() {
    static UITheme singleton;
    return singleton;
  }
  const ThemeMetrics& getMetrics() const { return metrics; }
  int getStatusBarHeight();
  int getProgressBarHeight();
};
struct {
  bool charging = false;
  bool isUsbConnected() const { return charging; }
} gpio;
struct {
  uint16_t percent = 57;
  uint16_t getBatteryPercentage() const { return percent; }
} powerManager;
struct {
  bool available = true, valid = true;
  bool isAvailable() const { return available; }
  bool formatTime(char* out, size_t n, bool twelve) const {
    if (valid) snprintf(out, n, "%s", twelve ? "9:41 PM" : "21:41");
    return valid;
  }
} halClock;
struct BaseTheme {
  static constexpr int batteryPercentSpacing = 4;
  virtual ~BaseTheme() = default;
  virtual uint8_t batteryFillStyle() const { return 0; }
  static void drawBatteryOutline(const GfxRenderer&, int, int, int, int);
  static void drawBatteryLightningBolt(const GfxRenderer&, int, int);
  virtual void fillBatteryIcon(const GfxRenderer&, Rect, uint16_t) const;
  void drawBatteryLeft(const GfxRenderer&, Rect, bool) const;
  static void drawStatusBar(GfxRenderer&, float, int, int, std::string, int, int, bool, bool, bool);
};
struct LyraTheme : BaseTheme {
  uint8_t batteryFillStyle() const override { return 1; }
  void fillBatteryIcon(const GfxRenderer&, Rect, uint16_t) const override;
};
static BaseTheme classic;
static LyraTheme lyra;
static BaseTheme* active = &classic;
#define GUI (*active)
static GfxRenderer* target;
#include "status_native.inc"
extern "C" size_t xp_test_live_allocations();
extern "C" size_t xp_test_allocation_attempts();
extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" void xp_app_test_reader_status(const XpEpubStatusFacts*, const XpEpubStatus*);
static const XpGlyphTarget* bandOverride;
extern "C" uint8_t xp_native_render_target(XpPageTarget* out, XpUiPaint*) {
  target->pageTarget(*out, false);
  if (bandOverride) out->glyphs = *bandOverride;
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
    static EpdFont small(&ubuntu_8_regular), estimate(&ubuntu_10_regular);
    renderer.insertFont(SMALL_FONT_ID, EpdFontFamily(&small));
    renderer.insertFont(UI_10_FONT_ID, EpdFontFamily(&estimate));
    renderer.setFontCacheManager(&cache);
    if (font) {
      assert(sd.load(font));
      renderer.insertFont(-99, EpdFontFamily(sd.getEpdFont(0), sd.getEpdFont(1), sd.getEpdFont(2), sd.getEpdFont(3)));
      renderer.registerSdCardFont(-99, &sd);
      renderer.setFallbackFont(SMALL_FONT_ID, -99);
      if (primary) {
        renderer.insertFont(SMALL_FONT_ID,
                            EpdFontFamily(sd.getEpdFont(0), sd.getEpdFont(1), sd.getEpdFont(2), sd.getEpdFont(3)));
        renderer.registerSdCardFont(SMALL_FONT_ID, &sd);
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
  if (argc == 2) assert(rust.renderer.resolveTextFontId(SMALL_FONT_ID, "한글", EpdFontFamily::REGULAR) == -99);
  size_t test = 0;
  auto exercise = [&](int orientation, const std::string& title, bool strips, bool scan) {
    native.orient(orientation);
    rust.orient(orientation);
    XpEpubStatus value{};
    value.page = 17;
    value.pages = 41;
    value.book_progress = 42.5f;
    value.building = test % 2;
    value.bookmarked = (test / 2) % 2;
    value.y_offset = (test / 4) % 2 ? 5 : 0;
    value.title = {reinterpret_cast<const uint8_t*>(title.data()), static_cast<uint32_t>(title.size())};
    auto drawNative = [&] {
      BaseTheme::drawStatusBar(native.renderer, value.book_progress, value.page, value.pages, title, 0, value.y_offset,
                               true, value.bookmarked, value.building);
    };
    auto drawRust = [&] {
      target = &rust.renderer;
      XpEpubStatusFacts facts{};
      readFacts(&facts);
      xp_app_test_reader_status(&facts, &value);
    };
    native.display.pixels.fill(0x69);
    rust.display.pixels.fill(0x69);
    if (scan) {
      auto ns = native.cache.createPrewarmScope();
      auto rs = rust.cache.createPrewarmScope();
      drawNative();
      drawRust();
      compare(native.display.pixels, rust.display.pixels, "scan", test);
      ns.endScanAndPrewarm();
      rs.endScanAndPrewarm();
      native.display.pixels.fill(0x69);
      rust.display.pixels.fill(0x69);
      drawNative();
      drawRust();
      compare(native.display.pixels, rust.display.pixels, "after scan", test);
    } else {
      drawNative();
      drawRust();
      compare(native.display.pixels, rust.display.pixels, "draw", test);
    }
    if (strips) {
      auto expected = native.display.pixels;
      std::array<uint8_t, HalDisplay::BUFFER_SIZE> assembled{};
      std::array<uint8_t, 16 * HalDisplay::DISPLAY_WIDTH_BYTES + 2> band{};
      for (int row = 0; row < HalDisplay::DISPLAY_HEIGHT; row += 16) {
        band.fill(0x69);
        const int rows = std::min(16, int(HalDisplay::DISPLAY_HEIGHT) - row);
        const int bytes = rows * HalDisplay::DISPLAY_WIDTH_BYTES;
        band.front() = band[bytes + 1] = 0xA5;
        XpPageTarget snapshot{};
        rust.renderer.pageTarget(snapshot, false);
        snapshot.glyphs.buffer = band.data() + 1;
        snapshot.glyphs.bufferLen = bytes;
        snapshot.glyphs.originY = row;
        snapshot.glyphs.rows = rows;
        snapshot.glyphs.strip = 1;
        bandOverride = &snapshot.glyphs;
        drawRust();
        bandOverride = nullptr;
        assert(band.front() == 0xA5 && band[bytes + 1] == 0xA5);
        std::copy_n(band.data() + 1, bytes, assembled.data() + row * HalDisplay::DISPLAY_WIDTH_BYTES);
      }
      compare(expected, assembled, "strips", test);
    }
    ++test;
  };
  auto& s = SETTINGS.value;
  for (int orient = 0; orient < 4; ++orient)
    for (int style = 0; style < 2; ++style)
      for (int bits = 0; bits < 32; ++bits)
        for (int title = 0; title < 3; ++title)
          for (int clock = 0; clock < 3; ++clock)
            for (int bar = 0; bar < 3; ++bar)
              for (int thick = 0; thick < 3; ++thick) {
                s.showChapterPageCount = bits & 1;
                s.showBookProgressPercent = bits & 2;
                s.showBattery = bits & 4;
                s.showBatteryPercent = bits & 8;
                s.clock12h = bits & 16;
                s.titleMode = title;
                s.clockMode = clock;
                s.progressBarMode = bar;
                s.progressBarHeightPx = bar == 2 ? 0 : (thick + 1) * 2;
                active = style ? static_cast<BaseTheme*>(&lyra) : &classic;
                UITheme::getInstance().metrics.batteryWidth = active == &lyra ? 16 : 15;
                BoardConfig::ACTIVE.viewableInsets = {3, 5, 7, 9};
                BoardConfig::ACTIVE.bottomCornerRadius = test % 2 ? 24 : 0;
                BoardConfig::ACTIVE.statusBarOffsetY = test % 2 ? -2 : 0;
                gpio.charging = test % 3 == 0;
                exercise(orient, title == 2 ? "" : "A chapter title for this book", false, true);
              }
  for (int orientation = 0; orientation < 4; ++orientation)
    for (int style = 0; style < 2; ++style)
      for (const char* title : {"", "Short",
                                "This is a very long chapter title that must be truncated to fit between two busy "
                                "status clusters and remain readable",
                                "עברית العربية", "한글 제목 — 읽기", "W"}) {
        active = style ? static_cast<BaseTheme*>(&lyra) : &classic;
        UITheme::getInstance().metrics.batteryWidth = style ? 16 : 15;
        s = {true, true, 1, true, true, 2, false, 0, 6, 0};
        exercise(orientation, title, true, true);
      }
  // Nonpositive title space, ellipsis-only fit, RTC probe/format failure, and fill thresholds.
  for (int orientation = 0; orientation < 4; ++orientation)
    for (int style = 0; style < 2; ++style) {
      active = style ? static_cast<BaseTheme*>(&lyra) : &classic;
      UITheme::getInstance().metrics.batteryWidth = style ? 16 : 15;
      for (int state = 0; state < 3; ++state)
        for (int percent : {0, 10, 11, 40, 41, 70, 71, 100}) {
          halClock.available = state != 0;
          halClock.valid = state != 1;
          powerManager.percent = percent;
          gpio.charging = state == 1;
          s = {true, true, 1, true, true, 2, true, 1, 4, 0};
          exercise(orientation, "A deliberately long title for edge cases", true, true);
        }
      halClock.available = halClock.valid = true;
      s = {false, false, 1, false, false, 0, false, 2, 0, 0};
      native.orient(orientation);
      rust.orient(orientation);
      // With no clusters the title reservation is 60 pixels, plus horizontal margins.
      const int width = rust.renderer.getScreenWidth();
      BoardConfig::ACTIVE.viewableInsets = {0, 0, 0, 0};
      // Set all insets equally so the rotated left+right sum is unchanged.
      const int inset = (width - 70 - 2) / 2;
      BoardConfig::ACTIVE.viewableInsets = {inset, inset, inset, inset};
      assert(rust.renderer.truncatedText(SMALL_FONT_ID, "WWWW", 2) == "…");
      exercise(orientation, "WWWW", true, true);
      BoardConfig::ACTIVE.viewableInsets = {width, width, width, width};
      exercise(orientation, "No positive title width", true, true);
    }
  BoardConfig::ACTIVE.viewableInsets = {};
  // Fail the actual Rust title allocation before font work, then verify recovery.
  target = &rust.renderer;
  s = {false, false, 1, false, false, 0, false, 2, 0, 0};
  XpEpubStatusFacts facts{};
  readFacts(&facts);
  const char* longTitle = "This title needs fallible terminated storage";
  XpEpubStatus value{};
  value.title = {reinterpret_cast<const uint8_t*>(longTitle), static_cast<uint32_t>(strlen(longTitle))};
  rust.display.pixels.fill(0x69);
  auto untouched = rust.display.pixels;
  auto live = xp_test_live_allocations();
  xp_test_fail_allocation_after(0);
  xp_app_test_reader_status(&facts, &value);
  assert(xp_test_allocation_attempts() == 1 && xp_test_live_allocations() == live);
  assert(rust.display.pixels == untouched);
  xp_test_fail_allocation_after(SIZE_MAX);
  xp_app_test_reader_status(&facts, &value);
  assert(rust.display.pixels != untouched);
  std::cout << test << " cases, " << comparisons << " exact framebuffer comparisons\n";
}
