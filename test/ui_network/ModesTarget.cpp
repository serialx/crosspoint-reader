#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <fontIds.h>
#include <xp_ui_network.h>

#include <cassert>
#include <cstdio>

#include "Trace.h"
void textGoldenGlyph(const EpdFontFamily&, uint32_t, int, int, EpdFontFamily::Style) {}
void textGoldenScaled(int, uint32_t, int, int, EpdFontFamily::Style) {}
namespace ui = freeink::ui;
namespace fui = freeink::ui;
using UiScreen = ui::Screen<24>;
struct ThemeMetrics {
  int topPadding = 12, headerHeight = 36, buttonHintsHeight = 40, verticalSpacing = 8;
} metrics;
struct UITheme {
  static UITheme& getInstance() {
    static UITheme t;
    return t;
  }
  const ThemeMetrics& getMetrics() { return metrics; }
};
struct NetworkModeSelectionActivity {
  int MENU_ITEM_COUNT;
  static constexpr auto ACTION_ROW = 1;
  ui::ListNav nav;
  ui::ListItem rowItems_[4]{};
  ui::ListNav& activeNav() { return nav; }
  int listCount() const { return MENU_ITEM_COUNT; }
  void buildScreen(UiScreen&);
  void syncListViewport(UiScreen&, ui::ListProps&, int = 0);
};
#include "modes_original.inc"
int main() {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  static EpdFont small(&ubuntu_10_regular), smallBold(&ubuntu_10_bold), body(&ubuntu_12_regular),
      bodyBold(&ubuntu_12_bold);
  renderer.insertFont(UI_10_FONT_ID, EpdFontFamily(&small, &smallBold));
  renderer.insertFont(UI_12_FONT_ID, EpdFontFamily(&body, &bodyBold));
  FontDecompressor decompressor;
  assert(decompressor.init());
  FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
  const char* labels[] = {"Join a Network", "Connect to Calibre", "Create Hotspot", "USB Drive"};
  const char* descriptions[] = {"Connect to an existing WiFi network with a deliberately long description that wraps",
                                "Transfer books from a Calibre library over the local wireless network",
                                "Create a wireless network", "Connect to a computer"};
  static const uint8_t icon[128] = {0xAA, 0xCC, 0xF0, 0x0F};
  unsigned cases = 0;
  for (int orientation = 0; orientation < 4; ++orientation)
    for (bool touch : {false, true})
      for (int count : {3, 4})
        for (int selected = 0; selected < count; ++selected)
          for (int selection = 0; selection < 4; ++selection) {
            renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
            renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
            ui::GfxRendererTarget target(renderer, touch);
            target.setFont(0, UI_10_FONT_ID);
            target.setFont(1, UI_12_FONT_ID);
            auto device = target.deviceContext();
            device.safeArea = {};
            auto theme = ui::themeTokensForLineHeight(renderer.getLineHeight(UI_12_FONT_ID));
            theme.listSelectionStyle = static_cast<ui::SelectionStyle>(selection);
            theme.listRowGap = 3;
            theme.listInset = 5;
            theme.listRowRadius = 12;
            ui::InteractionBuffer<24> hits;
            ui::Frame<24> frame(target, device, {}, hits);
            UiScreen screen(frame, theme);
            NetworkModeSelectionActivity original{count};
            original.nav.reset(selected);
            for (int i = 0; i < count; ++i) {
              auto& row = original.rowItems_[i];
              row.label = labels[i];
              row.subtitle = descriptions[i];
              row.icon = {icon, 32, 32};
              row.actionValue = i;
            }
            renderer.clearScreen();
            original.buildScreen(screen);
            const auto expected = display.pixels;
            struct Owner {
              GfxRenderer* renderer;
              ui::GfxRendererTarget* target;
              NetworkModeSelectionActivity* original;
            } owner{&renderer, &target, &original};
            XpUiSettingsResources resources{&owner,
                                            [](void* context, uint16_t index, XpUiSettingsRow* out) -> uint8_t {
                                              const auto& row =
                                                  static_cast<Owner*>(context)->original->rowItems_[index];
                                              *out = {row.label, row.subtitle, row.value, 1, 0, 0, 1, 0};
                                              return 1;
                                            },
                                            [](void* context, const char* text, int32_t font, uint8_t bold) -> int32_t {
                                              return static_cast<Owner*>(context)->renderer->getTextWidth(
                                                  font, text, static_cast<EpdFontFamily::Style>(bold));
                                            },
                                            [](void* context, const char* text, int32_t font, uint8_t bold,
                                               uint8_t lines, int16_t width) -> int32_t {
                                              auto& target = *static_cast<Owner*>(context)->target;
                                              target.setFont(0, font);
                                              ui::TextStyle style;
                                              style.font = 0;
                                              style.bold = bold;
                                              style.maxLines = lines;
                                              return ui::measureWrappedText(target, text, style, width).height;
                                            },
                                            [](void* context, int32_t font) -> int32_t {
                                              return static_cast<Owner*>(context)->renderer->getLineHeight(font);
                                            },
                                            static_cast<uint16_t>(count),
                                            0};
            XpUiSettingsLayout layout{};
            layout.list.screen = {0, 0, device.width, device.height};
            layout.list.area = layout.list.screen;
            layout.list.font = UI_12_FONT_ID;
            layout.small_font = UI_10_FONT_ID;
            layout.list.row_height = theme.listMinRowHeight;
            layout.list.row_gap = theme.listRowGap;
            layout.list.inset = theme.listInset;
            layout.list.padding = theme.listSidePadding;
            layout.list.scroll_width = theme.listScrollWidth;
            layout.list.radius = theme.listRowRadius;
            layout.list.selection = selection;
            layout.list.scroll_left = theme.listScrollSide;
            layout.list.bold = theme.bodyText.bold;
            layout.top_padding = 12;
            layout.header_height = 36;
            layout.hints_height = 40;
            layout.spacing = 8;
            layout.touch = touch;
            XpUiListState state{}, next{};
            xp_ui_settings_list_init(&state, selected);
            XpUiSettingsHits routing{};
            XpUiScene scene{};
            XpUiCommand commands[24]{};
            XpUiBuffer buffer{commands, nullptr, 24, 0};
            assert(xp_ui_network_modes_view(&state, &layout, &resources, &buffer, &next, &scene, &routing) == 1);
            XpUiPaint paint{
                &owner, nullptr, nullptr,
                [](void*, uint32_t, uint32_t generation) -> uint8_t { return generation == 1; },
                [](void* context, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
                  if (command->resource == XP_UI_NETWORK_HEADER || command->resource == XP_UI_NETWORK_FOOTER) return;
                  auto& self = *static_cast<Owner*>(context);
                  const auto& row = self.original->rowItems_[(command->resource - 1) / 4];
                  const auto r = command->rect;
                  if (command->kind == XP_UI_BITMAP) {
                    const auto old = self.renderer->getClipRect();
                    self.renderer->setClipRect(clip.x, clip.y, clip.w, clip.h);
                    self.target->bitmap({r.x, r.y, r.w, r.h}, row.icon, ui::BitmapMode::Contain,
                                        ui::Paint::solid(command->paint == 1 ? ui::Color::White : ui::Color::Black));
                    self.renderer->setClipRect(old[0], old[1], old[2], old[3]);
                  } else
                    self.renderer->drawUiText(*command, clip,
                                              (command->resource - 1) % 4 == 0 ? row.label : row.subtitle);
                }};
            assert(renderer.drawUiScene(scene, &paint));
            if (display.pixels != expected) {
              fprintf(stderr, "Mode pixels differ: orientation=%d touch=%d count=%d selected=%d style=%d\n",
                      orientation, touch, count, selected, selection);
              return 1;
            }
            assert(next.top == original.nav.top && next.drawn == original.nav.drawnRows);
            ++cases;
          }
  printf("%u mode body pixel cases passed\n", cases);
}
