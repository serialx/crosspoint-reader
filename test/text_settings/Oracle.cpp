// Frozen from dc21f50f TextSettings/UiTabListActivity geometry.

#include <BoardConfig.h>
#include <EpdFont.h>
#include <FreeInkApp.h>
#include <builtinFonts/ubuntu_10_regular.h>
#include <builtinFonts/ubuntu_12_regular.h>
#include <components/themes/lyra/LyraTheme.h>
#include <components/themes/roundedraff/RoundedRaffTheme.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
namespace fui = freeink::ui;
using UiScreen = fui::Screen<32>;
struct GfxRenderer {
  enum Orientation {
    Portrait,                  // 480x800 logical coordinates (current default)
    LandscapeClockwise,        // 800x480 logical coordinates, rotated 180° (swap top/bottom)
    PortraitInverted,          // 480x800 logical coordinates, inverted
    LandscapeCounterClockwise  // 800x480 logical coordinates, native panel orientation
  };
  Orientation orientation = Portrait;
  int panelWidth = 800, panelHeight = 480;
  Rect header{};
  mutable Rect preview{}, caption{};
  mutable std::string captionText;
  mutable std::array<int, 4> clip{0, 0, 2000, 2000};
  int getScreenWidth() const;
  int getScreenHeight() const;
  Orientation getOrientation() const { return orientation; }
  void getOrientedViewableTRBL(int*, int*, int*, int*) const;
  int getTextHeight(int) const { return ubuntu_10_regular.ascender; }
  void drawText(int, int x, int y, const char* text) const {
    EpdFont font(&ubuntu_10_regular);
    int width, height;
    font.getTextDimensions(text, &width, &height);
    caption = Rect{x, y, width, ubuntu_10_regular.advanceY};
    captionText = text;
  }
  std::array<int, 4> getClipRect() const { return clip; }
  void setClipRect(int x, int y, int w, int h) const { clip = {x, y, w, h}; }
};
struct UITheme {
  ThemeMetrics metrics;
  static UITheme& getInstance() {
    static UITheme instance;
    return instance;
  }
  const ThemeMetrics& getMetrics() const { return metrics; }
  Rect getContentArea(const GfxRenderer&);
  Rect getScreenSafeArea(const GfxRenderer&, bool, bool);
  Rect getHeaderRect(const GfxRenderer&, bool, bool);
  void drawHeader(GfxRenderer& renderer, Rect rect, const char*) { renderer.header = rect; }
};
#define GUI UITheme::getInstance()
#define tr(id) id
constexpr const char* STR_TEXT_SETTINGS = "Text Settings";
constexpr const char* STR_PREVIEW = "Preview";
constexpr int UI_10_FONT_ID = 0, TOUCH_TAB_BAR_HEIGHT = 50, ACTION_TAB = 1, ACTION_ROW = 2;
namespace textsettings {
struct PreviewLayout {};
void renderPreview(GfxRenderer& renderer, PreviewLayout& layout, int previewPadding, int labelGap, Rect bounds,
                   const char* caption) {
  renderer.preview = bounds;
  assert((renderer.clip == std::array<int, 4>{bounds.x, bounds.y, bounds.width, bounds.height}));
  const int top = bounds.y;
  const int height = bounds.height;
  const int left = bounds.x + previewPadding;
  const int width = bounds.width - (previewPadding * 2);
  if (width <= 0 || height <= 0) return;

  const int labelH = renderer.getTextHeight(UI_10_FONT_ID);
  const int labelReserved = labelH + labelGap + previewPadding;

  const int labelY = top + height - previewPadding - labelH;
  renderer.drawText(UI_10_FONT_ID, left, labelY, caption);
}

}  // namespace textsettings
struct UiTabListActivity {
  struct {
    bool touch = false;
    bool hasTouch() const { return touch; }
  } mappedInput;
  int tabPillMaxPad = 0, selected = 0;
  int tabCount() const { return 4; }
  const char* tabLabel(int index) const {
    static const char* labels[] = {"Font", "Size", "Layout", "Style"};
    return labels[index];
  }
  virtual int activeTab() const = 0;
  fui::TabIndicator tabIndicator(int) const { return fui::TabIndicator::None; }
  int ringPos() const { return selected; }
  void buildTabBar(UiScreen&);
};
struct TextSettingsActivity : UiTabListActivity {
  enum class Tab { Family, Size, Layout, Style };
  Tab tab_ = Tab::Layout;
  int activeTab() const override { return static_cast<int>(tab_); }
  GfxRenderer renderer;
  ThemeMetrics metrics_;
  int afterHeader = 0, previewHeight = 0;
  struct Entry {
    std::string name;
  };
  std::vector<Entry> fonts_{{"Bitter"}}, sizes_{{"12 pt"}};
  int currentFamilyIndex_ = 0, currentSizeIndex_ = 0;
  textsettings::PreviewLayout previewLayout_;
  std::vector<fui::ListItem> rowItems_;
  fui::ListNav nav;
  fui::ListNav& activeNav() { return nav; }
  int listCount() const { return rowItems_.size(); }
  void syncListViewport(UiScreen&, fui::ListProps&, int);
  void syncTabListViewport(UiScreen& screen, fui::ListProps& props) { syncListViewport(screen, props, 1); }
  void enterGeometry() {
    metrics_ = UITheme::getInstance().getMetrics();
    const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const int safeBottom = safeArea.y + safeArea.height;
    afterHeader = safeArea.y + metrics_.topPadding + metrics_.headerHeight + metrics_.verticalSpacing;
    const int usableHeight = safeBottom - afterHeader - metrics_.verticalSpacing;
    previewHeight = usableHeight * metrics_.previewHeightPercent / 100;
  }
  void buildScreen(UiScreen& screen) {
    const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const int tabTop = afterHeader + previewHeight;
    const int rightInset = renderer.getScreenWidth() - (safeArea.x + safeArea.width);
    const int bottomInset = renderer.getScreenHeight() - (safeArea.y + safeArea.height);
    screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(tabTop), static_cast<int16_t>(rightInset),
                                                  static_cast<int16_t>(bottomInset), static_cast<int16_t>(safeArea.x)});

    buildTabBar(screen);
  }
  void buildRows(UiScreen& screen) {
    fui::ListProps props;
    props.items = rowItems_.data();
    props.count = static_cast<uint16_t>(rowItems_.size());
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
    props.valueInset = 8;               // air between the value and the row edge
    // Titles match the value's font size (smallText) so both sides of a row
    // read as one unit; labels that still don't fit wrap onto a second line.
    // maxLines=2 also marks the style explicitly set (see SettingsActivity).
    props.labelText = screen.theme().smallText;
    props.labelText.maxLines = 2;
    syncTabListViewport(screen, props);
    screen.list(props);
  }

  void drawChrome() {
    GUI.drawHeader(renderer, UITheme::getInstance().getHeaderRect(renderer, true, false), tr(STR_TEXT_SETTINGS));

    const char* familyName = (currentFamilyIndex_ >= 0 && currentFamilyIndex_ < static_cast<int>(fonts_.size()))
                                 ? fonts_[currentFamilyIndex_].name.c_str()
                                 : "";
    const char* sizeName = (currentSizeIndex_ >= 0 && currentSizeIndex_ < static_cast<int>(sizes_.size()))
                               ? sizes_[currentSizeIndex_].name.c_str()
                               : "";
    char labelBuf[128];
    snprintf(labelBuf, sizeof(labelBuf), "%s \"%s, %s\"", tr(STR_PREVIEW), familyName, sizeName);
    const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const Rect previewBounds{safeArea.x, afterHeader, safeArea.width, previewHeight};
    const auto [savedX, savedY, savedWidth, savedHeight] = renderer.getClipRect();
    const int clipLeft = std::max(previewBounds.x, savedX);
    const int clipTop = std::max(previewBounds.y, savedY);
    const int clipRight = std::min(previewBounds.x + previewBounds.width, savedX + savedWidth);
    const int clipBottom = std::min(previewBounds.y + previewBounds.height, savedY + savedHeight);
    renderer.setClipRect(clipLeft, clipTop, std::max(0, clipRight - clipLeft), std::max(0, clipBottom - clipTop));
    textsettings::renderPreview(renderer, previewLayout_, metrics_.previewPadding, metrics_.verticalSpacing,
                                previewBounds, labelBuf);
    renderer.setClipRect(savedX, savedY, savedWidth, savedHeight);
  }
};
void TextSettingsActivity::syncListViewport(UiScreen& screen, fui::ListProps& props, const int selectionOffset) {
  props.toggleCheckbox = true;
  props.toggleWidth = 28;
  props.toggleHeight = 28;
  props.partialTrailingRow = true;
  auto& n = activeNav();
  const int prevTop = n.top;
  const bool trusted = n.trusts(listCount());
  const int drawn = n.drawnRows;

  screen.syncListViewport(n, props, listCount(), selectionOffset);

  // When the selection is already visible in the current viewport (based on
  // the measured drawnRows rather than the unweighted visibleRows estimate),
  // keep selection-follow anchored instead of jumping to top. Explicit swipe
  // scrolling clears followPending and must retain its new viewport.
  if (n.followPending && trusted && drawn > 0) {
    const int sel = props.selectedIndex;
    if (sel >= prevTop && sel < prevTop + drawn) {
      n.top = prevTop;
      props.topIndex = static_cast<uint16_t>(prevTop);
    }
  }
}
int GfxRenderer::getScreenWidth() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 480px wide in portrait logical coordinates
      return panelHeight;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 800px wide in landscape logical coordinates
      return panelWidth;
  }
  return panelHeight;
}
int GfxRenderer::getScreenHeight() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 800px tall in portrait logical coordinates
      return panelWidth;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 480px tall in landscape logical coordinates
      return panelHeight;
  }
  return panelWidth;
}
void GfxRenderer::getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const {
  // Board truth: the bezel insets live in the active profile (panel-native
  // portrait frame); this only rotates them into the current orientation.
  const BoardConfig::ViewableInsets& vi = BoardConfig::ACTIVE.viewableInsets;
  switch (orientation) {
    case Portrait:
      *outTop = vi.top;
      *outRight = vi.right;
      *outBottom = vi.bottom;
      *outLeft = vi.left;
      break;
    case LandscapeClockwise:
      *outTop = vi.left;
      *outRight = vi.top;
      *outBottom = vi.right;
      *outLeft = vi.bottom;
      break;
    case PortraitInverted:
      *outTop = vi.bottom;
      *outRight = vi.left;
      *outBottom = vi.top;
      *outLeft = vi.right;
      break;
    case LandscapeCounterClockwise:
      *outTop = vi.right;
      *outRight = vi.bottom;
      *outBottom = vi.left;
      *outLeft = vi.top;
      break;
  }
}
Rect UITheme::getContentArea(const GfxRenderer& renderer) {
  int viTop = 0, viRight = 0, viBottom = 0, viLeft = 0;
  renderer.getOrientedViewableTRBL(&viTop, &viRight, &viBottom, &viLeft);
  return Rect{viLeft, viTop, renderer.getScreenWidth() - viLeft - viRight,
              renderer.getScreenHeight() - viTop - viBottom};
}
Rect UITheme::getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints, bool hasSideButtonHints) {
  auto orientation = renderer.getOrientation();
  Rect safeArea = getContentArea(renderer);
  const ThemeMetrics metrics = getMetrics();
  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      if (hasFrontButtonHints) {
        safeArea.height -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      if (hasFrontButtonHints) {
        safeArea.x += metrics.buttonHintsHeight;
        safeArea.width -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      if (hasFrontButtonHints) {
        safeArea.y += metrics.buttonHintsHeight;
        safeArea.height -= metrics.buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      if (hasFrontButtonHints) {
        safeArea.width -= metrics.buttonHintsHeight;
      }
      break;
  }
  return safeArea;
}
Rect UITheme::getHeaderRect(const GfxRenderer& renderer, bool hasFrontButtonHints, bool hasSideButtonHints) {
  const Rect safe = getScreenSafeArea(renderer, hasFrontButtonHints, hasSideButtonHints);
  const Rect content = getContentArea(renderer);
  const ThemeMetrics metrics = getMetrics();
  return Rect{safe.x - content.x, safe.y - content.y + metrics.topPadding,
              safe.width + renderer.getScreenWidth() - content.width, metrics.headerHeight};
}

void UiTabListActivity::buildTabBar(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Tabs. The selected pill dims to a dither when the selection is down in
  // the list (the legacy focused/unfocused tab distinction).
  // Stack array, not a heap vector: this runs on every render and the tab
  // count is small and fixed.
  constexpr int MAX_TABS = 8;
  const int count = tabCount() < MAX_TABS ? tabCount() : MAX_TABS;
  fui::TabItem tabs[MAX_TABS];
  for (int i = 0; i < count; i++) {
    tabs[i].label = tabLabel(i);
    tabs[i].value = static_cast<int16_t>(i);
    tabs[i].selected = activeTab() == i;
    tabs[i].indicator = tabIndicator(i);
  }
  fui::TabBarProps tabProps;
  tabProps.tabs = tabs;
  tabProps.count = static_cast<uint16_t>(count);
  tabProps.action = ACTION_TAB;
  tabProps.inputMask = fui::InputTouch;
  // Pill shape and label size are theme-driven. Lyra uses equal-width slots
  // with small labels so wide text (e.g. "Controls") still fits at large UI scales.
  // Full-slot (RoundedRaff): the pill fills its slot like the legacy layout
  // (slot minus a 4px frame, 8px clearance above the divider) with
  // body-size labels; zero horizontal contentInset disables the tabBar's
  // label-width shrink.
  const bool tabsFocused = ringPos() == 0;
  if (metrics.tabPillFullSlot) {
    tabProps.text = screen.theme().bodyText;
    tabProps.tabInset = fui::Insets{4, 4, 7, 4};
    tabProps.contentInset = fui::Insets{2, 0, 2, 0};
  } else {
    tabProps.text = screen.theme().smallText;
    tabProps.gap = static_cast<int16_t>(metrics.tabSpacing);
    // Unfocused state: no bottom inset, so the pill (and the 2px selected
    // underline drawn along its bottom edge) reaches the band's 1px divider —
    // legacy Lyra drew the underline sitting on that rule, not floating above.
    tabProps.tabInset = tabsFocused ? fui::Insets{2, 4, 4, 4} : fui::Insets{2, 4, 0, 4};
    tabProps.contentInset = fui::Insets{2, 0, 2, 0};
  }
  const int16_t tabLineHeight = screen.target().lineHeight(tabProps.text.font);
  const int16_t preferredTabHeight =
      mappedInput.hasTouch() ? TOUCH_TAB_BAR_HEIGHT : static_cast<int16_t>(metrics.tabBarHeight);
  const int16_t tabBand = preferredTabHeight > tabLineHeight + 10 ? preferredTabHeight : tabLineHeight + 10;

  if (tabPillMaxPad > 0) {
    // Cap each pill at its label plus this padding: the equal-width slots (and
    // so the tab positions) stay exactly where they were, only the pill stops
    // stretching across the whole slot. The SDK shrinks the pill to content
    // width and centers it in its slot when the horizontal contentInset is
    // nonzero.
    tabProps.contentInset.left = tabPillMaxPad;
    tabProps.contentInset.right = tabPillMaxPad;
  }

  // Legacy Lyra two-state treatment: with the selection on the tab band, the
  // band fills gray and the active tab is a solid pill; with the selection
  // down in the list, the band is plain and the active tab keeps a gray box
  // with an underline. The 1px rule under the band is always there, drawn
  // full-width below (not by tabBar, whose rect is inset for side padding).
  fui::StyleSet tabStyles;
  tabStyles.explicitlySet = true;
  tabStyles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  if (tabsFocused) {
    tabStyles.selected.background = fui::Paint::solid(fui::Color::Black);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::White);
    tabStyles.selected.radius = screen.theme().listRowRadius;
  } else if (metrics.tabPillFullSlot) {
    // Legacy RoundedRaff unfocused treatment: same pill, dimmed to dark gray,
    // text stays inverted; no underline.
    tabStyles.selected.background = fui::Paint::dither(fui::Color::DarkGray);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::White);
    tabStyles.selected.radius = screen.theme().listRowRadius;
  } else {
    tabStyles.selected.background = fui::Paint::dither(fui::Color::LightGray);
    tabStyles.selected.foreground = fui::Paint::solid(fui::Color::Black);
    tabProps.selectedUnderline = 2;
  }
  // Focus/flash states keep the pill instead of falling back to an unset
  // (blank) style.
  tabStyles.focused = tabStyles.selected;
  tabStyles.active = tabStyles.selected;
  tabProps.tabStyles = tabStyles;
  const fui::Rect contentTabRect = screen.takeTop(tabBand);
  const fui::Rect frameRect = screen.frame().screen();
  const fui::Rect safeRect = screen.frame().safeRect();
  // The tab background reaches the display edges, but stops before the button labels.
  const fui::Rect tabRect{static_cast<int16_t>(contentTabRect.x - safeRect.x), contentTabRect.y,
                          static_cast<int16_t>(contentTabRect.width + frameRect.width - safeRect.width),
                          contentTabRect.height};
  // Focused band wash is the Lyra treatment; legacy RoundedRaff keeps the
  // band plain in both states.
  if (tabsFocused && !metrics.tabPillFullSlot) {
    screen.target().fill(tabRect, fui::Paint::dither(fui::Color::LightGray));
  }
  // Leave space on both sides of the tab labels and selection highlights.
  // Draw the line below the tabs across the full background width.
  const auto divider = static_cast<int16_t>(std::max(1, metrics.headerUnderlineSize));
  const auto side = static_cast<int16_t>(metrics.contentSidePadding);
  const fui::Rect slotsRect{static_cast<int16_t>(contentTabRect.x + side), tabRect.y,
                            static_cast<int16_t>(contentTabRect.width - 2 * side),
                            static_cast<int16_t>(tabRect.height - divider)};
  tabProps.divider = false;
  fui::tabBar(screen.frame(), slotsRect, tabProps);
  screen.target().fill(fui::Rect{tabRect.x, static_cast<int16_t>(tabRect.bottom() - divider), tabRect.width, divider},
                       fui::Paint::solid(fui::Color::Black));
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
}

namespace freeink::ui {
struct GfxRendererTarget : DrawTarget {
  static constexpr FontId FONT_BODY = 1;
};
}  // namespace freeink::ui
struct Target : fui::GfxRendererTarget {
  uint64_t hash = 1469598103934665603ULL;
  void number(int64_t n) {
    for (int i = 0; i < 8; ++i) {
      hash ^= (uint64_t(n) >> (i * 8)) & 255;
      hash *= 1099511628211ULL;
    }
  }
  void rect(fui::Rect r) {
    number(r.x);
    number(r.y);
    number(r.width);
    number(r.height);
  }
  void record() { std::printf("%016llx\n", static_cast<unsigned long long>(hash)); }

  std::vector<fui::Rect> textRects, fills;
  std::vector<std::string> textStrings;
  std::vector<bool> textBold;
  fui::Rect clip{0, 0, 32767, 32767};
  bool clippedText = false;
  fui::Rect clipRect() const override { return clip; }
  bool setClipRect(fui::Rect rect) override {
    clip = rect;
    return true;
  }
  fui::Size measureText(fui::FontId font, const char* text, fui::TextStyle) const override {
    EpdFont epdFont(font == 0 ? &ubuntu_10_regular : &ubuntu_12_regular);
    int width, height;
    epdFont.getTextDimensions(text, &width, &height);
    return {static_cast<int16_t>(width), lineHeight(font)};
  }
  int16_t lineHeight(fui::FontId font) const override {
    return font == 0 ? ubuntu_10_regular.advanceY : ubuntu_12_regular.advanceY;
  }
  void fill(fui::Rect r, fui::Paint, uint8_t radius, uint8_t corners) override {
    number(1);
    rect(r);
    number(radius);
    number(corners);
    fills.push_back(r);
  }
  void stroke(fui::Rect r, fui::Paint, uint8_t width, uint8_t radius, uint8_t corners) override {
    number(2);
    rect(r);
    number(width);
    number(radius);
    number(corners);
  }
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect rect, const char* text, fui::TextStyle style) override {
    number(3);
    this->rect(rect);
    for (const char* p = text; *p; ++p) number(static_cast<unsigned char>(*p));
    number(0);
    textStrings.emplace_back(text);
    textBold.push_back(style.bold);
    if (rect.y < clip.y || rect.bottom() > clip.bottom()) clippedText = true;
    const int16_t left = std::max(rect.x, clip.x);
    const int16_t top = std::max(rect.y, clip.y);
    textRects.push_back({left, top, static_cast<int16_t>(std::max(0, std::min(rect.right(), clip.right()) - left)),
                         static_cast<int16_t>(std::max(0, std::min(rect.bottom(), clip.bottom()) - top))});
  }
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
};
inline freeink::ui::ThemeTokens uiThemeTokens(const freeink::ui::GfxRendererTarget& target) {
  namespace fui = freeink::ui;
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();

  fui::ThemeTokens tokens = fui::themeTokensForLineHeight(target.lineHeight(fui::GfxRendererTarget::FONT_BODY));
  // Button-device lists retain the theme's preferred density. The SDK still
  // grows individual rows for wrapping, subtitles and icons.
  if (!BoardConfig::hasTouch()) tokens.listMinRowHeight = static_cast<int16_t>(metrics.listRowHeight);
  tokens.listRowGap = static_cast<int16_t>(metrics.listRowGap);
  tokens.listRowRadius = static_cast<uint8_t>(metrics.listRowRadius);
  tokens.listInset = static_cast<int16_t>(metrics.listInset);
  tokens.listSidePadding = static_cast<int16_t>(metrics.listSidePadding);
  tokens.listSelectionStyle = static_cast<fui::SelectionStyle>(metrics.listSelectionStyle);
  tokens.listScrollWidth = static_cast<int16_t>(metrics.listScrollWidth);
  tokens.listScrollSide = static_cast<uint8_t>(metrics.listScrollSide);
  // No extra scroll-track inset: the bezel is already compensated once at the
  // frame level (GfxRendererTarget::deviceContext() feeds the board's
  // viewable insets into the fui safe area, and every list band derives from
  // safeRect()). Adding the same inset here doubled the compensation and
  // floated the track a full bezel-width inside the visible edge.
  tokens.listScrollInset = 0;
  // Screen::header()/status() band height. Without this the SDK's
  // line-height-derived default applies and fui-drawn headers (OPDS) come out
  // a different height than every GUI.drawHeader band.
  tokens.headerHeight = static_cast<int16_t>(metrics.headerHeight);
  tokens.headerSidePadding = static_cast<int16_t>(metrics.headerSidePadding);
  tokens.headerUnderline = static_cast<uint8_t>(metrics.headerUnderlineSize);
  tokens.headerTitleAlign = static_cast<fui::TextAlign>(metrics.headerTitleAlign);
  // Control-panel shape (sheet corners, tiles, step buttons, capsule slider),
  // so the control center follows the theme like every list and header does.
  tokens.controlRadius = static_cast<uint8_t>(metrics.controlRadius);
  tokens.sheetRadius = static_cast<uint8_t>(metrics.sheetRadius);
  tokens.capsuleRadius = static_cast<uint8_t>(metrics.capsuleRadius);
  tokens.bodyText.bold = metrics.listTitleBold;
  return tokens;
}

void checkLayout(const BoardConfig::BoardProfile& profile, GfxRenderer::Orientation orientation, ThemeMetrics metrics,
                 TextSettingsActivity::Tab tab) {
  BoardConfig::ACTIVE = profile;
  const bool touch = BoardConfig::hasTouch();
  if (touch) metrics.buttonHintsHeight = 0;
  UITheme::getInstance().metrics = metrics;
  TextSettingsActivity activity;
  activity.tab_ = tab;
  activity.renderer.panelWidth = profile.displayWidth;
  activity.renderer.panelHeight = profile.displayHeight;
  activity.renderer.orientation = orientation;
  activity.mappedInput.touch = touch;
  const int width = activity.renderer.getScreenWidth();
  const int height = activity.renderer.getScreenHeight();
  int top, right, bottom, left;
  activity.renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const fui::Insets bezel{static_cast<int16_t>(top), static_cast<int16_t>(right), static_cast<int16_t>(bottom),
                          static_cast<int16_t>(left)};
  const Rect safe = UITheme::getInstance().getScreenSafeArea(activity.renderer, true, false);
  activity.enterGeometry();
  activity.drawChrome();
  const Rect preview = activity.renderer.preview;
  assert(preview.x == safe.x && preview.width == safe.width);
  assert(preview.y == safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing);
  assert(preview.y + preview.height <= safe.y + safe.height);
  assert(activity.renderer.header.x + bezel.left == safe.x);
  assert(activity.renderer.header.width == safe.width + bezel.left + bezel.right);
  assert((activity.renderer.clip == std::array<int, 4>{0, 0, 2000, 2000}));

  Target target;
  fui::DeviceContext device;
  device.width = width;
  device.height = height;
  device.safeArea = bezel;
  device.hasTouch = touch;
  fui::InputSnapshot input;
  fui::InteractionBuffer<32> interactions;
  fui::Frame<32> frame(target, device, input, interactions);
  auto theme = uiThemeTokens(target);
  for (int focus : {0, 1, 2, 3, 4, 1, 0}) {
    activity.selected = focus;
    activity.drawChrome();
    const Rect caption = activity.renderer.caption;
    assert(activity.renderer.captionText == "Preview \"Bitter, 12 pt\"");
    assert(caption.x == preview.x + metrics.previewPadding);
    assert(caption.x + caption.width <= preview.x + preview.width - metrics.previewPadding);
    assert(caption.y ==
           preview.y + preview.height - metrics.previewPadding - activity.renderer.getTextHeight(UI_10_FONT_ID));
    assert(caption.y + caption.height <= preview.y + preview.height);
    assert(activity.renderer.preview.x == preview.x && activity.renderer.preview.y == preview.y);
    assert(activity.renderer.preview.width == preview.width && activity.renderer.preview.height == preview.height);
    interactions.clear();
    target.fills.clear();
    UiScreen screen(frame, theme);
    activity.buildScreen(screen);
    const auto content = screen.contentRect();
    assert(content.x == safe.x && content.width == safe.width);
    assert(content.bottom() == safe.y + safe.height);
    assert(content.y >= preview.y + preview.height);
    assert(content.height > 0);
    assert(interactions.count() == 4);
    for (size_t i = 0; i < interactions.count(); ++i) {
      const auto tab = interactions.data()[i].rect;
      assert(tab.x >= safe.x && tab.right() <= safe.x + safe.width);
    }
    // Decoration can bleed under the bezel, but not into the button-hint strip.
    const int bandLeft = safe.x - bezel.left;
    const int bandRight = safe.x + safe.width + bezel.right;
    for (const auto& rect : target.fills) {
      assert(rect.x >= bandLeft && rect.right() <= bandRight);
    }
    assert(std::any_of(target.fills.begin(), target.fills.end(), [bandLeft, bandRight, metrics](fui::Rect rect) {
      return rect.x == bandLeft && rect.right() == bandRight && rect.height == std::max(1, metrics.headerUnderlineSize);
    }));

    // A scrolling settings list keeps values and the scroll track out of the hints.
    activity.rowItems_.assign(20, fui::ListItem{});
    for (auto& row : activity.rowItems_) {
      row.label = "Font";
      row.value = "Selected";
    }
    activity.nav.reset(1);
    target.textRects.clear();
    target.fills.clear();
    target.clippedText = false;
    activity.buildRows(screen);
    assert(!target.textRects.empty());
    for (const auto& rect : target.textRects) {
      assert(rect.x >= safe.x && rect.right() <= safe.x + safe.width);
      assert(rect.y >= content.y && rect.bottom() <= content.bottom());
    }
    assert(std::any_of(target.fills.begin(), target.fills.end(), [safe, content, theme](fui::Rect rect) {
      return rect.width == theme.listScrollWidth && rect.x >= safe.x && rect.right() <= safe.x + safe.width &&
             rect.y == content.y && rect.bottom() == content.bottom();
    }));
  }
  target.record();
}
void checkStyleRowsFit(const BoardConfig::BoardProfile& profile, GfxRenderer::Orientation orientation,
                       ThemeMetrics metrics) {
  BoardConfig::ACTIVE = profile;
  UITheme::getInstance().metrics = metrics;
  TextSettingsActivity activity;
  activity.tab_ = TextSettingsActivity::Tab::Style;
  activity.renderer.panelWidth = profile.displayWidth;
  activity.renderer.panelHeight = profile.displayHeight;
  activity.renderer.orientation = orientation;
  activity.enterGeometry();
  const char* labels[] = {"Focus Reading", "Hyphenation (no preview)", "Embedded Style (no preview)",
                          "Text Anti-Aliasing (no preview)"};
  for (const char* label : labels) {
    fui::ListItem row;
    row.label = label;
    row.toggle = true;
    activity.rowItems_.push_back(row);
  }

  Target target;
  fui::DeviceContext device;
  device.width = activity.renderer.getScreenWidth();
  device.height = activity.renderer.getScreenHeight();
  int top, right, bottom, left;
  activity.renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  device.safeArea = fui::Insets{static_cast<int16_t>(top), static_cast<int16_t>(right), static_cast<int16_t>(bottom),
                                static_cast<int16_t>(left)};
  fui::InputSnapshot input;
  fui::InteractionBuffer<32> interactions;
  fui::Frame<32> frame(target, device, input, interactions);
  auto theme = uiThemeTokens(target);
  for (bool checked : {false, true}) {
    for (auto& row : activity.rowItems_) row.toggleChecked = checked;
    for (int focus : {0, 1, 2, 3, 4}) {
      activity.selected = focus;
      activity.nav.reset(focus);
      interactions.clear();
      UiScreen screen(frame, theme);
      activity.buildScreen(screen);
      target.textRects.clear();
      target.textStrings.clear();
      target.clippedText = false;
      activity.buildRows(screen);
      if (activity.nav.drawnRows != 4) {
        std::fprintf(stderr, "%s orientation %d: list height %d, full rows %d\n", profile.name,
                     static_cast<int>(orientation), screen.contentRect().height, activity.nav.drawnRows);
      }
      assert(activity.nav.top == 0);
      assert(activity.nav.drawnRows == 4);
      assert(target.textRects.size() == 4);
      assert(!target.clippedText);
      assert(target.textStrings[0].find("(no preview)") == std::string::npos);
      for (int row = 0; row < 4; ++row) {
        assert(target.textStrings[row] == labels[row]);
        assert(target.measureText(theme.smallText.font, labels[row], theme.smallText).width <=
               target.textRects[row].width);
        if (row != 0) assert(target.textStrings[row].find("(no preview)") != std::string::npos);
      }
    }
  }
  target.record();
}
void checkSharedPartialRows() {
  Target target;
  fui::DeviceContext device;
  device.width = 800;
  device.height = 480;
  fui::InputSnapshot input;
  fui::InteractionBuffer<32> interactions;
  fui::Frame<32> frame(target, device, input, interactions);
  auto theme = fui::themeTokensForLineHeight(29);
  theme.listMinRowHeight = 40;
  TextSettingsActivity activity;
  activity.rowItems_.assign(10, fui::ListItem{});
  for (auto& row : activity.rowItems_) {
    row.label = "Paragraph Indentation";
    row.value = "2";
  }
  // Text Settings uses the shared next-row preview policy, including pixel clipping.
  for (int remainder : {4, 14, 18, 20, 24, 36}) {
    for (int selected : {1, 4}) {
      interactions.clear();
      target.textRects.clear();
      target.clippedText = false;
      activity.nav.reset(selected);
      UiScreen screen(frame, theme);
      screen.setContentMarginFromScreen(fui::Insets{100, 0, static_cast<int16_t>(260 - remainder), 0});
      const auto content = screen.contentRect();
      activity.buildRows(screen);
      const bool preview = remainder >= 18;
      assert(target.clippedText == preview);
      assert(activity.nav.drawnRows == 3);
      assert(target.textRects.size() == (preview ? 8 : 6));
      for (const auto& rect : target.textRects) {
        assert(rect.y >= content.y && rect.bottom() <= content.bottom());
      }
      assert(activity.nav.top <= selected - 1 && selected - 1 < activity.nav.top + 3);
    }
  }
  target.record();
}
int main() {
  using Tab = TextSettingsActivity::Tab;
  for (const auto& profile :
       {BoardConfig::XTEINK_X3, BoardConfig::XTEINK_X3_UC8279, BoardConfig::XTEINK_X4, BoardConfig::XTEINK_X4_CLASSIC})
    for (auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted, GfxRenderer::LandscapeClockwise,
                             GfxRenderer::LandscapeCounterClockwise})
      for (const auto& metrics : {LyraMetrics::values, RoundedRaffMetrics::values})
        checkStyleRowsFit(profile, orientation, metrics);
  for (const auto& profile :
       {BoardConfig::XTEINK_X3, BoardConfig::XTEINK_X3_UC8279, BoardConfig::XTEINK_X4, BoardConfig::XTEINK_X4_PRO,
        BoardConfig::XTEINK_X4_CLASSIC, BoardConfig::STICKY, BoardConfig::PAPER_MONO, BoardConfig::METALIO_EINK4,
        BoardConfig::EEGO_A4, BoardConfig::PICCO})
    for (auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted, GfxRenderer::LandscapeClockwise,
                             GfxRenderer::LandscapeCounterClockwise})
      for (const auto& metrics : {LyraMetrics::values, RoundedRaffMetrics::values})
        for (Tab tab : {Tab::Family, Tab::Size, Tab::Layout, Tab::Style})
          checkLayout(profile, orientation, metrics, tab);
  checkSharedPartialRows();
  return 0;
}
