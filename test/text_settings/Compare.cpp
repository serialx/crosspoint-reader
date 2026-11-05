#define main frozenMain
#include "Oracle.cpp"
#undef main
#include <xp_ui_text_settings.h>

#include <cstring>
struct Resources {
  Target& target;
  TextSettingsActivity& activity;
  static const char* text(void* raw, uint32_t h) {
    auto& self = *static_cast<Resources*>(raw);
    if (h >= XP_UI_TEXT_TAB_BASE && h < XP_UI_TEXT_TAB_BASE + 4) return self.activity.tabLabel(h - XP_UI_TEXT_TAB_BASE);
    if (h == 0 || h >= XP_UI_READER_HEADER) return nullptr;
    const auto index = (h - 1) / 4;
    if (index >= self.activity.rowItems_.size()) return nullptr;
    const auto& row = self.activity.rowItems_[index];
    return (h - 1) % 4 == 2 ? row.value : row.label;
  }
};
static unsigned maximumCommands = 0;
void compare(const BoardConfig::BoardProfile& profile, GfxRenderer::Orientation orientation, ThemeMetrics metrics,
             int tab, int focus, bool checked) {
  BoardConfig::ACTIVE = profile;
  if (BoardConfig::hasTouch()) metrics.buttonHintsHeight = 0;
  UITheme::getInstance().metrics = metrics;
  TextSettingsActivity a;
  a.renderer.panelWidth = profile.displayWidth;
  a.renderer.panelHeight = profile.displayHeight;
  a.renderer.orientation = orientation;
  a.tab_ = static_cast<TextSettingsActivity::Tab>(tab);
  a.selected = focus;
  a.nav.reset(focus);
  a.mappedInput.touch = BoardConfig::hasTouch();
  a.enterGeometry();
  const int count = tab < 2 ? 20 : tab == 2 ? 7 : 4;
  for (int i = 0; i < count; ++i) {
    fui::ListItem row;
    row.label = tab == 3 ? "Hyphenation (no preview)" : "Paragraph Indentation";
    row.value = tab == 3 ? nullptr : "Selected";
    row.toggle = tab == 3;
    row.toggleChecked = checked;
    a.rowItems_.push_back(row);
  }
  Target expected;
  fui::DeviceContext device;
  device.width = a.renderer.getScreenWidth();
  device.height = a.renderer.getScreenHeight();
  device.hasTouch = BoardConfig::hasTouch();
  int top, right, bottom, left;
  a.renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  device.safeArea = {static_cast<int16_t>(top), static_cast<int16_t>(right), static_cast<int16_t>(bottom),
                     static_cast<int16_t>(left)};
  fui::InputSnapshot input;
  fui::InteractionBuffer<32> interactions;
  fui::Frame<32> frame(expected, device, input, interactions);
  const auto tokens = uiThemeTokens(expected);
  UiScreen screen(frame, tokens);
  a.buildScreen(screen);
  a.buildRows(screen);
  XpUiTextState state{}, next{};
  xp_ui_text_init(&state, tab, 0, 0, 20, 20, 0);
  state.tabs[tab].selected = focus;
  XpUiTextLayout layout{};
  auto& t = layout.theme;
  const auto safe = UITheme::getInstance().getScreenSafeArea(a.renderer, true, false);
  t.screen = {0, 0, device.width, device.height};
  t.safe = {int16_t(safe.x), int16_t(safe.y), int16_t(safe.width), int16_t(safe.height)};
  t.body_font = tokens.bodyText.font;
  t.small_font = tokens.smallText.font;
  t.row_min = BoardConfig::hasTouch() ? 0 : metrics.listRowHeight;
  t.row_gap = metrics.listRowGap;
  t.row_inset = metrics.listInset;
  t.row_padding = metrics.listSidePadding;
  t.scroll_width = metrics.listScrollWidth;
  t.top_padding = metrics.topPadding;
  t.header_height = metrics.headerHeight;
  t.spacing = metrics.verticalSpacing;
  t.tab_height = metrics.tabBarHeight;
  t.row_radius = metrics.listRowRadius;
  t.selection = metrics.listSelectionStyle;
  t.scroll_left = metrics.listScrollSide;
  t.touch = BoardConfig::hasTouch();
  t.orientation = orientation;
  t.title_bold = metrics.listTitleBold;
  const auto bezel = device.safeRect();
  layout.bezel = {bezel.x, bezel.y, bezel.width, bezel.height};
  layout.preview_percent = metrics.previewHeightPercent;
  layout.preview_padding = metrics.previewPadding;
  layout.side_padding = metrics.contentSidePadding;
  layout.tab_spacing = metrics.tabSpacing;
  layout.underline = metrics.headerUnderlineSize;
  layout.full_slot = metrics.tabPillFullSlot;
  Resources res{expected, a};
  XpUiReaderRows rows{&res,
                      [](void*, uint16_t, uint16_t) -> uint8_t { return 1; },
                      [](void* p, uint16_t i, XpUiReaderRow* out) -> uint8_t {
                        auto& self = *static_cast<Resources*>(p);
                        const auto& row = self.activity.rowItems_[i];
                        const uint32_t h = i * 4 + 1;
                        *out = {h,     0, row.value ? h + 2 : 0, 0, uint8_t(row.toggle), uint8_t(row.toggleChecked),
                                {0, 0}};
                        return 1;
                      },
                      [](void* p, uint32_t h, int32_t f, uint8_t b) -> int32_t {
                        auto& r = *static_cast<Resources*>(p);
                        fui::TextStyle s;
                        s.font = f;
                        s.bold = b;
                        return r.target.measureText(f, Resources::text(p, h), s).width;
                      },
                      [](void* p, uint32_t h, int32_t f, uint8_t b, uint8_t lines, int16_t w) -> int32_t {
                        auto& r = *static_cast<Resources*>(p);
                        fui::TextStyle s;
                        s.font = f;
                        s.bold = b;
                        s.maxLines = lines;
                        return fui::measureWrappedText(r.target, Resources::text(p, h), s, w).height;
                      },
                      [](void* p, int32_t f) -> int32_t { return static_cast<Resources*>(p)->target.lineHeight(f); },
                      uint16_t(count),
                      64,
                      1};
  XpUiCommand commands[128]{};
  XpUiBuffer buffer{commands, nullptr, 128, 0};
  XpUiScene scene{};
  XpUiReaderHitLayout hits{};
  const auto result = xp_ui_text_view(&state, &layout, &rows, &buffer, &next, &scene, &hits);
  if (result != 1) {
    fprintf(stderr, "view failed: %s o%d tab%d focus%d status%d\n", profile.name, orientation, tab, focus, result);
    std::abort();
  }
  maximumCommands = std::max(maximumCommands, unsigned(scene.command_count));
  const auto savedNext = next;
  const auto savedScene = scene;
  const auto savedHits = hits;
  buffer.command_capacity = 1;
  assert(xp_ui_text_view(&state, &layout, &rows, &buffer, &next, &scene, &hits) == 2);
  assert(memcmp(&savedNext, &next, sizeof(next)) == 0);
  assert(memcmp(&savedScene, &scene, sizeof(scene)) == 0);
  assert(memcmp(&savedHits, &hits, sizeof(hits)) == 0);
  buffer.command_capacity = 128;
  assert(xp_ui_text_view(&state, &layout, &rows, &buffer, &next, &scene, &hits) == 1);
  Target actual;
  std::vector<fui::Rect> clips;
  for (size_t i = 0; i < scene.command_count; ++i) {
    const auto& c = commands[i];
    const auto r = fui::Rect{c.rect.x, c.rect.y, c.rect.w, c.rect.h};
    if (c.kind == XP_UI_CLIP) {
      clips.push_back(actual.clip);
      actual.clip = r;
    } else if (c.kind == XP_UI_POP) {
      actual.clip = clips.back();
      clips.pop_back();
    } else if (c.kind == XP_UI_TEXT_HANDLE) {
      fui::TextStyle s;
      s.font = c.font;
      s.bold = c.style & 1;
      s.maxLines = c.max_lines;
      actual.text(r, Resources::text(&res, c.resource), s);
    }
  }
  if (expected.textRects.size() != actual.textRects.size()) {
    fprintf(stderr, "text count %s o%d tab%d focus%d: %zu %zu\n", profile.name, orientation, tab, focus,
            expected.textRects.size(), actual.textRects.size());
    std::abort();
  }
  for (size_t i = 0; i < expected.textRects.size(); ++i) {
    const auto e = expected.textRects[i], v = actual.textRects[i];
    if (memcmp(&e, &v, sizeof(e)) || expected.textStrings[i] != actual.textStrings[i]) {
      fprintf(stderr, "text %zu %s o%d tab%d focus%d: [%d,%d,%d,%d] != [%d,%d,%d,%d]\n", i, profile.name, orientation,
              tab, focus, e.x, e.y, e.width, e.height, v.x, v.y, v.width, v.height);
      std::abort();
    }
  }
  assert(expected.textBold == actual.textBold);
  assert(next.tabs[tab].drawn == a.nav.drawnRows);
  assert(next.tabs[tab].top == a.nav.top);
}
int main() {
  int cases = 0;
  for (const auto& profile : {BoardConfig::XTEINK_X3, BoardConfig::XTEINK_X4, BoardConfig::XTEINK_X4_PRO})
    for (auto orientation : {GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise, GfxRenderer::PortraitInverted,
                             GfxRenderer::LandscapeCounterClockwise})
      for (const auto& metrics : {ThemeMetrics{}, LyraMetrics::values, RoundedRaffMetrics::values})
        for (int tab = 0; tab < 4; ++tab)
          for (int focus : {0, 1, 3})
            for (bool checked : {false, true}) {
              compare(profile, orientation, metrics, tab, focus, checked);
              ++cases;
            }
  printf("%d C++/Rust text-settings placement cases passed; maximum %u commands\n", cases, maximumCommands);
}
