#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
struct Rect {
  int x, y, width, height;
};
struct GfxRenderer {
  enum class Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  int width = 528, height = 792;
  Orientation orientation = Orientation::Portrait;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  Orientation getOrientation() const { return orientation; }
  void getOrientedViewableTRBL(int* t, int* r, int* b, int* l) const {
    *t = 7;
    *r = 11;
    *b = 13;
    *l = 17;
  }
  int getTextWidth(int, const char* text) const { return std::strlen(text) * 7; }
  int getLineHeight(int) const { return 20; }
};
struct Input {
  bool touch = false;
  bool hasTouch() const { return touch; }
};
struct Metrics {
  int verticalSpacing = 10, menuSpacing = 8, topPadding = 12, contentSidePadding = 16;
  int headerHeight = 32, buttonHintsHeight = 40, sideButtonHintsWidth = 36;
};
struct UITheme {
  Metrics metrics;
  static UITheme& getInstance() {
    static UITheme instance;
    return instance;
  }
  const Metrics& getMetrics() const { return metrics; }
  Rect getScreenSafeArea(const GfxRenderer& r, bool, bool) const { return {5, 9, r.width - 14, r.height - 51}; }
};
struct Scale {
  int smallFontId = 1;
};
Scale uiScaleSpec() { return {}; }
enum { STR_LOOKUP, STR_CLIP, STR_BOOKMARK_OPTION };
const char* tr(int id) { return id == STR_LOOKUP ? "Look up" : id == STR_CLIP ? "Clip" : "Bookmark"; }
constexpr int TOUCH_PAGE_END_DWELL_SLOP_PX = 8;
// GEOMETRY
struct ClipSelectionActivity {
  enum class Mode { Clip, Lookup, DefinitionLookup };
  struct WordBox {
    int16_t x, y, width, height;
    uint16_t row;
    uint8_t pageOffset;
    bool isRtl;
  };
  GfxRenderer renderer;
  Input mappedInput;
  std::vector<WordBox> words;
  size_t wordCount = 0;
  int selected = 0, rangeStart = -1, currentPageOffset = 0, lineHeight = 20, rowCount = 0, updates = 0;
  bool touchDragHasMoved = false;
  Mode mode = Mode::Clip;
  size_t pageCount() const { return 3; }
  void requestUpdate() { ++updates; }
  int closestInRow(uint16_t, int) const;
  int wordAt(int, int) const;
  int dragWordAt(int, int) const;
  bool selectionContains(int, int) const;
  int nextPageStartIndexForTouchDrag() const;
  bool isWithinCurrentPageEndDwellSlop(int, int) const;
  void moveVertical(int);
  void selectIndex(int);
  void moveToPage(int);
  Rect handleRect(int, bool) const;
  int selectionTop() const;
  int actionAt(int, int) const;
  Rect actionRect() const;
  int textOffset() const;
  int textXOffset() const;
};
struct DictionaryDefinitionActivity {
  struct BodyArea {
    int x, y, width, height;
  };
  GfxRenderer renderer;
  BodyArea bodyArea() const;
};
// METHODS
void rect(Rect r) { std::printf(" %d %d %d %d", r.x, r.y, r.width, r.height); }
int main() {
  for (int width : {480, 528, 792, 800}) {
    for (int orientation = 0; orientation < 4; ++orientation) {
      for (int theme = 0; theme < 3; ++theme) {
        auto& metrics = UITheme::getInstance().metrics;
        metrics.verticalSpacing = 8 + theme * 2;
        metrics.menuSpacing = 6 + theme * 2;
        metrics.contentSidePadding = 12 + theme * 4;
        DictionaryDefinitionActivity definition;
        definition.renderer = {width, width > 528 ? 480 : 792, static_cast<GfxRenderer::Orientation>(orientation)};
        auto body = definition.bodyArea();
        std::printf("body %d %d %d %d %d %d %d\n", width, orientation, theme, body.x, body.y, body.width, body.height);
        for (bool touch : {false, true}) {
          for (bool rtl : {false, true}) {
            ClipSelectionActivity a;
            a.renderer = definition.renderer;
            a.mappedInput.touch = touch;
            for (int i = 0; i < 240; ++i)
              a.words.push_back({static_cast<int16_t>(-4 + i % 8 * 64), static_cast<int16_t>(4 + (i / 8 % 10) * 62),
                                 static_cast<int16_t>(24 + i % 3 * 10), 24, static_cast<uint16_t>(i / 8),
                                 static_cast<uint8_t>(i / 80), rtl});
            a.wordCount = a.words.size();
            a.rowCount = 30;
            for (int selected : {0, 1, 7, 40, 79, 80, 159, 239}) {
              a.selected = selected;
              a.currentPageOffset = a.words[selected].pageOffset;
              a.rangeStart = selected > 0 ? selected - 1 : 0;
              a.touchDragHasMoved = true;
              for (auto mode : {ClipSelectionActivity::Mode::Clip, ClipSelectionActivity::Mode::DefinitionLookup}) {
                a.mode = mode;
                std::printf("selection %d %d %d %d %d %d", selected, touch, rtl, int(mode), a.textXOffset(),
                            a.textOffset());
                rect(a.handleRect(a.rangeStart, true));
                rect(a.handleRect(selected, false));
                rect(a.actionRect());
                std::printf(" next %d\n", a.nextPageStartIndexForTouchDrag());
                const auto word = a.words[selected];
                for (int dx : {-9, -4, -3, 0, 24, 58, 75}) {
                  const int x = word.x + dx, y = word.y + 10 + a.textOffset();
                  std::printf("hit %d %d %d %d %d\n", a.wordAt(x, y), a.dragWordAt(x, y), a.selectionContains(x, y),
                              a.actionAt(x, y), a.isWithinCurrentPageEndDwellSlop(x, y));
                }
              }
              a.moveVertical(-1);
              a.moveVertical(1);
              a.moveToPage(a.currentPageOffset + 1);
              std::printf("move %d %d %d\n", a.selected, a.currentPageOffset, a.updates);
            }
          }
        }
      }
    }
  }
}
