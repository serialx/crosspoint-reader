#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <fontIds.h>
#include <xp_ui_settings.h>

#include <array>
#include <cassert>
#include <cstdio>

#include "Trace.h"
void textGoldenGlyph(const EpdFontFamily&, uint32_t, int, int, EpdFontFamily::Style) {}
void textGoldenScaled(int, uint32_t, int, int, EpdFontFamily::Style) {}
namespace ui = freeink::ui;
struct Owner {
  GfxRenderer& renderer;
  ui::GfxRendererTarget& target;
  std::array<ui::ListItem, 12> rows;
};
int main() {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  static EpdFont small(&ubuntu_10_regular), smallBold(&ubuntu_10_bold);
  static EpdFont body(&ubuntu_12_regular), bodyBold(&ubuntu_12_bold);
  renderer.insertFont(UI_10_FONT_ID, EpdFontFamily(&small, &smallBold));
  renderer.insertFont(UI_12_FONT_ID, EpdFontFamily(&body, &bodyBold));
  FontDecompressor decompressor;
  assert(decompressor.init());
  FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
  unsigned cases = 0;
  for (uint8_t kind : {XP_UI_SETTINGS_SECTION, XP_UI_SETTINGS_LANGUAGE})
    for (int orientation = 0; orientation < 4; ++orientation)
      for (bool touch : {false, true})
        for (bool smallLabels : {false, true})
          for (int selected : {0, 2, 9})
            for (int selection : {0, 1, 2, 3}) {
              renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
              renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
              ui::GfxRendererTarget target(renderer, touch);
              target.setFont(0, UI_10_FONT_ID);
              target.setFont(1, UI_12_FONT_ID);
              Owner owner{renderer, target, {}};
              for (int i = 0; i < 12; ++i) {
                auto& row = owner.rows[i];
                row.label = i % 3 == 0 ? "A long setting name that wraps across the available space" : "Setting";
                row.value = i % 3 == 0 ? "Selected" : nullptr;
                row.toggle = i % 3 == 1;
                row.toggleChecked = i % 2 == 0;
                row.subtitle = i % 4 == 0 ? "Remote status" : nullptr;
                row.enabled = i != 2;
                row.actionValue = i;
              }
              auto theme = ui::themeTokensForLineHeight(renderer.getLineHeight(UI_12_FONT_ID));
              theme.listSelectionStyle = static_cast<ui::SelectionStyle>(selection);
              theme.listRowRadius = 12;
              theme.listRowGap = 3;
              theme.listInset = 5;
              ui::DeviceContext device = target.deviceContext();
              device.safeArea = {};
              device.height = 240;
              ui::ListNav nav;
              nav.reset(selected);
              for (int pass = 0; pass < 9; ++pass) {
                renderer.clearScreen();
                ui::InteractionBuffer<32> hits;
                ui::Frame<32> frame(target, device, {}, hits);
                ui::Screen<32> screen(frame, theme);
                screen.setContentMarginFromScreen(kind == XP_UI_SETTINGS_LANGUAGE ? ui::Insets{55, 5, 30, 5}
                                                                                  : ui::Insets{45, 0, 20, 0});
                screen.spacer(8);
                ui::ListProps props;
                props.items = owner.rows.data();
                props.count = owner.rows.size();
                props.action = 1;
                props.inputMask = ui::InputTouch;
                props.partialTrailingRow = true;
                props.toggleCheckbox = true;
                props.toggleWidth = props.toggleHeight = 28;
                props.valueInset = kind == XP_UI_SETTINGS_LANGUAGE ? 0 : 8;
                props.labelText =
                    touch && kind == XP_UI_SETTINGS_SECTION && !smallLabels ? theme.bodyText : theme.smallText;
                props.labelText.maxLines = 2;
                screen.syncListViewport(nav, props, owner.rows.size());
                screen.list(props);
                if (!nav.consumeRebuildNeeded()) break;
              }
              const auto expected = display.pixels;
              const XpUiSettingsResources services{
                  &owner,
                  [](void* p, uint16_t i, XpUiSettingsRow* out) -> uint8_t {
                    const auto& r = static_cast<Owner*>(p)->rows[i];
                    *out = {r.label, r.subtitle, r.value, 0, r.toggle, r.toggleChecked, r.enabled, 0};
                    return 1;
                  },
                  [](void* p, const char* text, int32_t font, uint8_t bold) -> int32_t {
                    return static_cast<Owner*>(p)->renderer.getTextWidth(font, text,
                                                                         static_cast<EpdFontFamily::Style>(bold));
                  },
                  [](void* p, const char* text, int32_t font, uint8_t bold, uint8_t lines, int16_t width) -> int32_t {
                    auto& target = static_cast<Owner*>(p)->target;
                    target.setFont(0, font);
                    ui::TextStyle style;
                    style.font = 0;
                    style.bold = bold;
                    style.maxLines = lines;
                    return ui::measureWrappedText(target, text, style, width).height;
                  },
                  [](void* p, int32_t font) -> int32_t { return static_cast<Owner*>(p)->renderer.getLineHeight(font); },
                  12,
                  0};
              XpUiSettingsLayout layout{};
              layout.list.screen = {0, 0, device.width, device.height};
              layout.list.area = layout.list.screen;
              layout.safe = {5, 10, static_cast<int16_t>(device.width - 10), 200};
              layout.top_padding = 5;
              layout.header_height = 40;
              layout.hints_height = 20;
              layout.spacing = 8;
              layout.list.font = UI_12_FONT_ID;
              layout.small_font = UI_10_FONT_ID;
              layout.list.row_gap = 3;
              layout.list.inset = 5;
              layout.list.padding = 8;
              layout.list.scroll_width = 3;
              layout.list.radius = 12;
              layout.list.selection = selection;
              layout.kind = kind;
              layout.flags = smallLabels ? XP_UI_SETTINGS_SMALL_LABELS_ON_TOUCH : 0;
              layout.touch = touch;
              XpUiListState state{}, next{};
              xp_ui_settings_list_init(&state, selected);
              XpUiCommand commands[128];
              XpUiBuffer buffer{commands, nullptr, 128, 0};
              XpUiScene scene{};
              XpUiSettingsHits hits{};
              assert(xp_ui_settings_view(&state, &layout, &services, &buffer, &next, &scene, &hits) == 1);
              XpUiPaint paint{
                  &owner, nullptr, nullptr,
                  [](void*, uint32_t, uint32_t generation) -> uint8_t { return generation == 1; },
                  [](void* p, const XpGlyphTarget*, const XpUiCommand* c, XpUiRect clip) {
                    if (c->resource >= 0x80000000u) return;
                    auto& owner = *static_cast<Owner*>(p);
                    const auto& row = owner.rows[(c->resource - 1) / 4];
                    const auto field = (c->resource - 1) % 4;
                    owner.renderer.drawUiText(*c, clip, field == 0 ? row.label : field == 1 ? row.subtitle : row.value);
                  }};
              assert(renderer.drawUiScene(scene, &paint));
              if (display.pixels != expected) {
                std::fprintf(stderr,
                             "settings pixels differ: kind=%d orientation=%d touch=%d selected=%d style=%d nav=%d/%d "
                             "drawn=%d/%d\n",
                             kind, orientation, touch, selected, selection, nav.top, next.top, nav.drawnRows,
                             next.drawn);
                return 1;
              }
              assert(nav.top == next.top && nav.drawnRows == next.drawn);
              ++cases;
            }
  std::printf("%u settings cases match SDK pixels and viewport feedback\n", cases);
}
