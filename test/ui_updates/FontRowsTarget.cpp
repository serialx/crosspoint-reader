#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <fontIds.h>
#include <xp_ui_updates.h>

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
  std::array<ui::ListItem, 80> rows;
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
  unsigned maximumCommands = 0;
  std::printf("body line=%d small line=%d\n", renderer.getLineHeight(UI_12_FONT_ID),
              renderer.getLineHeight(UI_10_FONT_ID));
  for (uint8_t kind : {XP_UI_UPDATE_FONTS})
    for (int orientation = 0; orientation < 4; ++orientation)
      for (bool touch : {false, true})
        for (bool subtitles : {false, true})
          for (int selected : {0, 2, 9})
            for (int selection : {0, 1, 2, 3}) {
              renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
              renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
              ui::GfxRendererTarget target(renderer, touch);
              target.setFont(0, UI_10_FONT_ID);
              target.setFont(1, UI_12_FONT_ID);
              Owner owner{renderer, target, {}};
              for (int i = 0; i < 80; ++i) {
                auto& row = owner.rows[i];
                row.label = i % 3 == 0 ? "A long setting name that wraps across the available space" : "Setting";
                row.value = "Selected";
                row.toggle = false;
                row.toggleChecked = i % 2 == 0;
                row.subtitle = subtitles && i % 4 == 0 ? "Remote status" : nullptr;
                row.enabled = true;
                row.actionValue = i;
              }
              auto theme = ui::themeTokensForLineHeight(renderer.getLineHeight(UI_12_FONT_ID));
              theme.listSelectionStyle = static_cast<ui::SelectionStyle>(selection);
              theme.listRowRadius = 12;
              theme.listRowGap = 0;
              theme.listMinRowHeight = 30;
              theme.listInset = 5;
              ui::DeviceContext device = target.deviceContext();
              device.safeArea = {};
              device.height = renderer.getScreenHeight();
              ui::ListNav nav;
              nav.reset(selected);
              for (int pass = 0; pass < 1; ++pass) {
                renderer.clearScreen();
                ui::InteractionBuffer<32> hits;
                ui::Frame<32> frame(target, device, {}, hits);
                ui::Screen<32> screen(frame, theme);
                screen.setContentMarginFromScreen(ui::Insets{45, 0, 20, 0});
                screen.spacer(8);
                ui::ListProps props;
                props.items = owner.rows.data();
                props.count = owner.rows.size();
                props.action = 1;
                props.inputMask = ui::InputTouch;
                props.partialTrailingRow = true;
                props.toggleCheckbox = false;
                props.toggleWidth = props.toggleHeight = 28;
                props.valueInset = 8;
                props.labelText = theme.bodyText;
                props.labelText.maxLines = 1;
                screen.syncListViewport(nav, props, owner.rows.size());
                screen.list(props);
                if (!nav.consumeRebuildNeeded()) break;
              }
              const auto expected = display.pixels;
              XpUiUpdateResources updateServices{};
              updateServices.rows = XpUiSettingsResources{
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
                  80,
                  0};
              updateServices.context = &owner;
              updateServices.text = [](void*, uint32_t) -> const char* { return ""; };
              XpUiUpdateLayout updateLayout{};
              auto& layout = updateLayout.list;
              updateLayout.progress_height = 24;
              XpUiUpdateFeedback feedback{};
              layout.list.screen = {0, 0, device.width, device.height};
              layout.list.area = layout.list.screen;
              layout.safe = {5, 10, static_cast<int16_t>(device.width - 10), 200};
              layout.top_padding = 5;
              layout.header_height = 40;
              layout.hints_height = 20;
              layout.spacing = 8;
              layout.list.font = UI_12_FONT_ID;
              layout.small_font = UI_10_FONT_ID;
              layout.help_font = UI_10_FONT_ID;
              layout.list.row_height = touch ? 0 : theme.listMinRowHeight;
              layout.list.row_gap = 0;
              layout.list.inset = 5;
              layout.list.padding = 8;
              layout.list.scroll_width = 3;
              layout.list.radius = 12;
              layout.list.selection = selection;
              layout.kind = 0;
              layout.touch = touch;
              XpUiUpdateState state{};
              xp_ui_updates_init(&state, XP_UI_UPDATE_FONTS, 0);
              state.phase = 3;
              state.list.selected = selected;
              auto& next = feedback.list;
              XpUiCommand commands[128];
              XpUiBuffer buffer{commands, nullptr, 128, 0};
              XpUiScene scene{};
              assert(xp_ui_updates_view(&state, &updateLayout, &updateServices, &buffer, &feedback, &scene) == 1);
              maximumCommands = std::max<unsigned>(maximumCommands, scene.command_count);
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
  assert(maximumCommands <= 71);
  std::printf("%u dense font-list cases match SDK pixels and viewport feedback, max commands=%u\n", cases,
              maximumCommands);
}
