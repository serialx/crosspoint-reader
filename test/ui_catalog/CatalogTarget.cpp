#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <fontIds.h>
#include <xp_ui_catalog.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <numeric>

#include "../../src/components/icons/opdsIcons.h"
#include "Trace.h"
void textGoldenGlyph(const EpdFontFamily&, uint32_t, int, int, EpdFontFamily::Style) {}
void textGoldenScaled(int, uint32_t, int, int, EpdFontFamily::Style) {}
namespace ui = freeink::ui;
struct Owner {
  GfxRenderer& renderer;
  ui::GfxRendererTarget& target;
  std::array<ui::ListItem, 32> rows;
};
namespace fui = ui;
struct UiAppHost {
  using UiScreen = ui::Screen<32>;
};
struct CatalogLine {
  const char* text;
  bool bold = false;
};
struct UITheme {
  struct Metrics {
    int contentSidePadding = 20;
  };
  static UITheme& getInstance() {
    static UITheme theme;
    return theme;
  }
  Metrics getMetrics() const { return {}; }
};
enum { STR_DOWNLOADING, STR_CANCEL };
const char* tr(int id) { return id == STR_DOWNLOADING ? "Downloading" : "Cancel"; }
#include "status_original.inc"
static const char* statusText(uint32_t handle) {
  switch (handle) {
    case 0x80000003:
      return "A long synthetic book title that wraps onto a second line";
    case 0x80000004:
      return "No entries";
    case 0x80000005:
      return "Error";
    case 0x80000006:
      return "The fixture server could not return its catalog. Please try again.";
    case 0x80000007:
      return "Tap to retry";
    case 0x80000008:
      return "Downloading";
    case 0x80000009:
      return "1.5 MB";
    case 0x8000000a:
      return "Cancel";
    default:
      return "";
  }
}
static bool statusCases(GfxRenderer& renderer, HalDisplay& display) {
  unsigned cases = 0;
  for (int orientation = 0; orientation < 4; ++orientation)
    for (bool touch : {false, true})
      for (bool bold : {false, true})
        for (int mode : {XP_UI_CATALOG_LOADING, XP_UI_CATALOG_BROWSING, XP_UI_CATALOG_ERROR, XP_UI_CATALOG_DOWNLOADING})
          for (bool known : {false, true}) {
            renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
            renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
            ui::GfxRendererTarget target(renderer, touch);
            target.setFont(0, UI_10_FONT_ID);
            target.setFont(1, UI_12_FONT_ID);
            Owner owner{renderer, target, {}};
            auto theme = ui::themeTokensForLineHeight(renderer.getLineHeight(UI_12_FONT_ID));
            theme.bodyText.bold = bold;
            theme.controlRadius = 6;
            auto device = target.deviceContext();
            device.safeArea = {};
            device.height = 360;
            ui::InteractionBuffer<32> expectedHits;
            ui::Frame<32> frame(target, device, {}, expectedHits);
            ui::Screen<32> screen(frame, theme);
            screen.setContentMarginFromScreen({48, 0, 36, 0});
            renderer.clearScreen();
            switch (mode) {
              case XP_UI_CATALOG_LOADING:
                screen.centeredText(statusText(0x80000003), theme.bodyText);
                break;
              case XP_UI_CATALOG_BROWSING:
                screen.centeredText(statusText(0x80000004), theme.bodyText);
                break;
              case XP_UI_CATALOG_ERROR:
                if (touch)
                  catalogCenteredBlock(screen,
                                       {{statusText(0x80000005)}, {statusText(0x80000006)}, {statusText(0x80000007)}});
                else
                  catalogCenteredBlock(screen, {{statusText(0x80000005)}, {statusText(0x80000006)}});
                break;
              default:
                catalogDownloadScreen(screen, statusText(0x80000003), 1572864, known ? 4194304 : 0,
                                      XP_UI_CATALOG_CANCEL_TRANSFER);
            }
            const auto expected = display.pixels;
            const XpUiCatalogResources services{
                &owner,
                [](void*, uint16_t, XpUiCatalogRow*) -> uint8_t { return 0; },
                [](void*, uint32_t h) { return statusText(h); },
                [](void* p, const char* s, int32_t f, uint8_t b) -> int32_t {
                  return static_cast<Owner*>(p)->renderer.getTextWidth(f, s, static_cast<EpdFontFamily::Style>(b));
                },
                [](void* p, const char* s, int32_t f, uint8_t b, uint8_t l, int16_t w) -> int32_t {
                  auto& t = static_cast<Owner*>(p)->target;
                  t.setFont(0, f);
                  ui::TextStyle style;
                  style.font = 0;
                  style.bold = b;
                  style.maxLines = l;
                  return ui::measureWrappedText(t, s, style, w).height;
                },
                [](void* p, int32_t f) -> int32_t { return static_cast<Owner*>(p)->renderer.getLineHeight(f); },
                0,
                0};
            XpUiCatalogLayout layout{};
            layout.list.screen = {0, 0, device.width, device.height};
            layout.list.area = layout.list.screen;
            layout.list.font = UI_12_FONT_ID;
            layout.list.bold = bold;
            layout.small_font = UI_10_FONT_ID;
            layout.header_height = 40;
            layout.spacing = 8;
            layout.hints_height = 36;
            layout.side_padding = 20;
            layout.touch = touch;
            layout.space_md = theme.spaceMd;
            layout.button_height = theme.rowHeight;
            layout.control_radius = 6;
            XpUiCatalogState state{};
            xp_ui_catalog_init(&state, XP_UI_CATALOG_BROWSER, -1, 0);
            state.mode = mode;
            state.progress = 1572864;
            state.total = known ? 4194304 : 0;
            XpUiCommand commands[64];
            XpUiBuffer buffer{commands, nullptr, 64, 0};
            XpUiScene scene{};
            XpUiListState next{};
            XpUiCatalogHits hits{};
            assert(xp_ui_catalog_view(&state, &layout, &services, &buffer, &next, &scene, &hits) == 1);
            XpUiPaint paint{&owner, nullptr, nullptr, [](void*, uint32_t, uint32_t g) -> uint8_t { return g == 1; },
                            [](void* p, const XpGlyphTarget*, const XpUiCommand* c, XpUiRect clip) {
                              if (c->resource == 0x80000001 || c->resource == 0x80000002) return;
                              static_cast<Owner*>(p)->renderer.drawUiText(*c, clip, statusText(c->resource));
                            }};
            assert(renderer.drawUiScene(scene, &paint));
            if (display.pixels != expected) {
              std::fprintf(stderr, "status differs: orientation=%d touch=%d bold=%d mode=%d known=%d\n", orientation,
                           touch, bold, mode, known);
              return false;
            }
            ++cases;
          }
  std::printf("%u frozen catalog status pixel cases match\n", cases);
  return true;
}

static bool paginationCases(GfxRenderer& renderer, HalDisplay& display) {
  static constexpr const freeink::Icon* icons[] = {&icon_page_first_32, &icon_page_prev_32, &icon_page_next_32,
                                                   &icon_page_last_32};
  unsigned cases = 0;
  for (int orientation = 0; orientation < 4; ++orientation)
    for (int height : {40, 48, 60})
      for (uint8_t links = 1; links < 16; ++links) {
        renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
        renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
        ui::GfxRendererTarget target(renderer);
        target.setFont(0, UI_10_FONT_ID);
        target.setFont(1, UI_12_FONT_ID);
        Owner owner{renderer, target, {}};
        auto device = target.deviceContext();
        device.safeArea = {};
        device.height = 360;
        ui::InteractionBuffer<32> expectedHits;
        ui::Frame<32> frame(target, device, {}, expectedHits);
        const int16_t y = static_cast<int16_t>(device.height - 36 - height);
        renderer.clearScreen();
        target.fill({0, y, device.width, 1}, ui::Paint::solid(ui::Color::Black));
        int16_t zone = (device.width - 40) / 3;
        for (bool right : {false, true}) {
          ui::TabItem tabs[2];
          int count = 0;
          for (int index = right ? 2 : 0; index < (right ? 4 : 2); ++index) {
            if ((index == 0 || index == 3) && !(links & (1 << index))) continue;
            tabs[count].icon = ui::bitmapFromIcon(*icons[index]);
            tabs[count].enabled = links & (1 << index);
            tabs[count].value = index;
            ++count;
          }
          ui::TabBarProps props;
          props.tabs = tabs;
          props.count = count;
          props.action = XP_UI_CATALOG_PAGE;
          props.inputMask = ui::InputTouch;
          props.iconSize = 32;
          ui::tabBar(frame,
                     {static_cast<int16_t>(right ? device.width - 20 - zone : 20), static_cast<int16_t>(y + 1), zone,
                      static_cast<int16_t>(height - 1)},
                     props);
        }
        const auto expected = display.pixels;
        XpUiCatalogState state{};
        xp_ui_catalog_init(&state, XP_UI_CATALOG_BROWSER, -1, 0);
        XpUiCatalogLayout layout{};
        layout.list.screen = {0, 0, device.width, device.height};
        layout.list.area = layout.list.screen;
        layout.list.font = UI_12_FONT_ID;
        layout.small_font = UI_10_FONT_ID;
        layout.hints_height = 36;
        layout.tab_height = height;
        layout.side_padding = 20;
        layout.pages = links;
        XpUiCatalogResources services{
            &owner,
            [](void*, uint16_t, XpUiCatalogRow*) -> uint8_t { return 0; },
            [](void*, uint32_t) -> const char* { return ""; },
            [](void*, const char*, int32_t, uint8_t) -> int32_t { return 0; },
            [](void*, const char*, int32_t, uint8_t, uint8_t, int16_t) -> int32_t { return 24; },
            [](void* p, int32_t f) -> int32_t { return static_cast<Owner*>(p)->renderer.getLineHeight(f); },
            0,
            0};
        XpUiCommand commands[32];
        XpUiBuffer buffer{commands, nullptr, 32, 0};
        XpUiScene scene{};
        XpUiListState next{};
        XpUiCatalogHits hits{};
        assert(xp_ui_catalog_view(&state, &layout, &services, &buffer, &next, &scene, &hits) == 1);
        XpUiPaint paint{&owner, nullptr, nullptr, [](void*, uint32_t, uint32_t g) -> uint8_t { return g == 1; },
                        [](void* p, const XpGlyphTarget*, const XpUiCommand* c, XpUiRect) {
                          if (c->resource < 0x80000010 || c->resource > 0x80000013) return;
                          const auto r = c->rect;
                          static_cast<Owner*>(p)->target.bitmap(
                              {r.x, r.y, r.w, r.h}, ui::bitmapFromIcon(*icons[c->resource - 0x80000010]),
                              ui::BitmapMode::Contain, ui::Paint::solid(ui::Color::Black));
                        }};
        assert(renderer.drawUiScene(scene, &paint));
        if (display.pixels != expected) {
          std::fprintf(stderr, "pagination differs: orientation=%d height=%d links=%d\n", orientation, height, links);
          return false;
        }
        assert(hits.control_count == expectedHits.count());
        for (unsigned i = 0; i < hits.control_count; ++i) {
          const auto& a = hits.controls[i];
          const auto& b = expectedHits.data()[i];
          assert(a.rect.x == b.rect.x && a.rect.y == b.rect.y && a.rect.w == b.rect.width &&
                 a.rect.h == b.rect.height && a.value == b.value);
        }
        ++cases;
      }
  std::printf("%u pagination pixel and hit cases match SDK\n", cases);
  return true;
}

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
  if (!statusCases(renderer, display) || !paginationCases(renderer, display)) return 1;
  unsigned cases = 0;
  unsigned maxCommands[3]{};
  for (bool fullHeight : {false, true})
    for (uint8_t kind : {XP_UI_CATALOG_SERVERS, XP_UI_CATALOG_EDITOR, XP_UI_CATALOG_BROWSER})
      for (int orientation = 0; orientation < 4; ++orientation)
        for (bool touch : {false, true})
          for (int selected : {0, 2, 9})
            for (int selection : {0, 1, 2, 3}) {
              renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
              renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
              ui::GfxRendererTarget target(renderer, touch);
              target.setFont(0, UI_10_FONT_ID);
              target.setFont(1, UI_12_FONT_ID);
              Owner owner{renderer, target, {}};
              for (int i = 0; i < 32; ++i) {
                auto& row = owner.rows[i];
                row.label = i % 3 == 0 ? "A long setting name that wraps across the available space" : "Setting";
                row.value = i % 3 == 0 ? "Selected" : nullptr;
                row.sectionHeading = kind == XP_UI_CATALOG_BROWSER && (i == 0 || i == 3) ? "Publications" : nullptr;
                row.subtitle = i % 4 == 0 ? "Remote status" : nullptr;
                row.actionValue = i;
              }
              auto theme = ui::themeTokensForLineHeight(renderer.getLineHeight(UI_12_FONT_ID));
              theme.listSelectionStyle = static_cast<ui::SelectionStyle>(selection);
              theme.listRowRadius = 12;
              theme.listRowGap = 3;
              theme.listInset = 5;
              ui::DeviceContext device = target.deviceContext();
              device.safeArea = {};
              if (!fullHeight) device.height = 240;
              ui::ListNav nav;
              nav.reset(selected);
              for (int pass = 0; pass < 9; ++pass) {
                renderer.clearScreen();
                ui::InteractionBuffer<32> hits;
                ui::Frame<32> frame(target, device, {}, hits);
                ui::Screen<32> screen(frame, theme);
                screen.setContentMarginFromScreen(kind == XP_UI_CATALOG_BROWSER ? ui::Insets{53, 0, 20, 0}
                                                                                : ui::Insets{55, 5, 50, 5});
                if (kind != XP_UI_CATALOG_BROWSER) screen.spacer(8);
                ui::ListProps props;
                props.items = owner.rows.data();
                props.count = owner.rows.size();
                props.action = 1;
                props.inputMask = ui::InputTouch;
                props.partialTrailingRow = true;
                props.toggleCheckbox = true;
                props.toggleWidth = props.toggleHeight = 28;
                props.valueInset = kind == XP_UI_CATALOG_SERVERS ? 0 : 8;
                if (kind == XP_UI_CATALOG_EDITOR) {
                  props.labelText = theme.smallText;
                  props.labelText.maxLines = 2;
                }
                screen.syncListViewport(nav, props, owner.rows.size());
                screen.list(props);
                if (!nav.consumeRebuildNeeded()) break;
              }
              const auto expected = display.pixels;
              const XpUiCatalogResources services{
                  &owner,
                  [](void* p, uint16_t i, XpUiCatalogRow* out) -> uint8_t {
                    const auto& r = static_cast<Owner*>(p)->rows[i];
                    *out = {r.label, r.subtitle, r.value, r.sectionHeading};
                    return 1;
                  },
                  [](void*, uint32_t) -> const char* { return ""; },
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
                  32,
                  0};
              XpUiCatalogLayout layout{};
              layout.list.screen = {0, 0, device.width, device.height};
              layout.list.area = layout.list.screen;
              layout.safe = {5, 10, static_cast<int16_t>(device.width - 10), static_cast<int16_t>(device.height - 40)};
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
              layout.touch = touch;
              XpUiCatalogState state{};
              XpUiListState next{};
              xp_ui_catalog_init(&state, kind, -1, 0);
              state.list.selected = selected;
              XpUiCommand commands[128];
              XpUiBuffer buffer{commands, nullptr, 128, 0};
              XpUiScene scene{};
              XpUiCatalogHits hits{};
              assert(xp_ui_catalog_view(&state, &layout, &services, &buffer, &next, &scene, &hits) == 1);
              XpUiPaint paint{&owner, nullptr, nullptr,
                              [](void*, uint32_t, uint32_t generation) -> uint8_t { return generation == 1; },
                              [](void* p, const XpGlyphTarget*, const XpUiCommand* c, XpUiRect clip) {
                                if (c->resource >= 0x80000000u) return;
                                auto& owner = *static_cast<Owner*>(p);
                                const auto& row = owner.rows[(c->resource - 1) / 4];
                                const auto field = (c->resource - 1) % 4;
                                owner.renderer.drawUiText(*c, clip,
                                                          field == 0   ? row.label
                                                          : field == 1 ? row.subtitle
                                                          : field == 2 ? row.value
                                                                       : row.sectionHeading);
                              }};
              assert(renderer.drawUiScene(scene, &paint));
              if (display.pixels != expected) {
                std::fprintf(stderr,
                             "catalog pixels differ: kind=%d orientation=%d touch=%d selected=%d style=%d nav=%d/%d "
                             "drawn=%d/%d\n",
                             kind, orientation, touch, selected, selection, nav.top, next.top, nav.drawnRows,
                             next.drawn);
                return 1;
              }
              assert(nav.top == next.top && nav.drawnRows == next.drawn);
              maxCommands[kind] = std::max(maxCommands[kind], unsigned(scene.command_count));
              ++cases;
            }
  std::printf("%u catalog cases match SDK pixels and viewport feedback; max commands %u/%u/%u\n", cases, maxCommands[0],
              maxCommands[1], maxCommands[2]);
}
