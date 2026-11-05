#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <fontIds.h>
#include <xp_plugin.h>
#include <xp_ui_catalog.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <numeric>

#include "../../src/components/icons/opdsIcons.h"
#include "../../src/components/icons/search32.h"
#include "Trace.h"
void textGoldenGlyph(const EpdFontFamily&, uint32_t, int, int, EpdFontFamily::Style) {}
void textGoldenScaled(int, uint32_t, int, int, EpdFontFamily::Style) {}
namespace ui = freeink::ui;
struct Owner {
  GfxRenderer& renderer;
  ui::GfxRendererTarget& target;
  std::array<ui::ListItem, 32> rows;
  const char* pageText = "";
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
#define STR_PLUGIN_AUTH_WAITING 2
#include "plugin_original.inc"
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
    case 0x8000000d:
      return "Download complete";
    case 0x8000000e:
      return "Sign in using the device code to browse this catalog";
    case 0x80000014:
      return "https://example.invalid/activate";
    case 0x80000015:
      return "TEST-ABCD";
    case 0x80000016:
      return "Cancel";
    case 0x80000018:
      return "Plugin Hub";
    case 0x80000019:
      return "Install";
    case 0x8000001a:
      return "Hide";
    case 0x8000001b:
      return "Cancel";
    default:
      return "";
  }
}
static bool statusCases(GfxRenderer& renderer, HalDisplay& display) {
  unsigned cases = 0;
  unsigned maxCommands[5]{};
  for (int orientation = 0; orientation < 4; ++orientation)
    for (bool touch : {false, true})
      for (bool bold : {false, true})
        for (int family : {XP_UI_CATALOG_BROWSER, XP_UI_CATALOG_PLUGINS, XP_UI_CATALOG_HUB})
          for (int mode : {XP_UI_CATALOG_LOADING, XP_UI_CATALOG_BROWSING, XP_UI_CATALOG_ERROR,
                           XP_UI_CATALOG_DOWNLOADING, XP_UI_CATALOG_DONE, XP_UI_CATALOG_NO_TOKEN, XP_UI_CATALOG_AUTH})
            for (bool known : {false, true}) {
              if ((family == XP_UI_CATALOG_BROWSER && mode >= XP_UI_CATALOG_DONE) ||
                  (mode == XP_UI_CATALOG_AUTH && family != XP_UI_CATALOG_PLUGINS))
                continue;
              renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
              renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
              ui::GfxRendererTarget target(renderer, touch);
              target.setFont(0, UI_10_FONT_ID);
              target.setFont(1, UI_12_FONT_ID);
              target.setFont(2, UI_12_FONT_ID);
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
                    catalogCenteredBlock(screen, {{statusText(0x80000005), family != XP_UI_CATALOG_BROWSER},
                                                  {statusText(0x80000006)},
                                                  {statusText(0x80000007)}});
                  else
                    catalogCenteredBlock(
                        screen, {{statusText(0x80000005), family != XP_UI_CATALOG_BROWSER}, {statusText(0x80000006)}});
                  break;
                case XP_UI_CATALOG_DONE:
                  catalogCenteredBlock(screen, {{statusText(0x8000000d), true}, {statusText(0x80000003)}});
                  break;
                case XP_UI_CATALOG_NO_TOKEN:
                  catalogCenteredBlock(screen, {{statusText(0x8000000e)}});
                  break;
                case XP_UI_CATALOG_AUTH: {
                  fui::Rect qr;
                  pluginAuthScreen(screen, statusText(0x80000014), statusText(0x80000015), qr);
                  target.fill(qr, ui::Paint::solid(ui::Color::Black));
                  break;
                }
                default:
                  catalogDownloadScreen(screen, statusText(0x80000003), 1572864,
                                        family != XP_UI_CATALOG_PLUGINS && known ? 4194304 : 0,
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
              layout.title_font = UI_12_FONT_ID;
              layout.header_height = 40;
              layout.spacing = 8;
              layout.hints_height = 36;
              layout.side_padding = 20;
              layout.touch = touch;
              layout.space_md = theme.spaceMd;
              layout.button_height = theme.rowHeight;
              layout.control_radius = 6;
              XpUiCatalogState state{};
              xp_ui_catalog_init(&state, family, -1, 0);
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
                                if (c->resource == 0x80000017) {
                                  auto r = c->rect;
                                  static_cast<Owner*>(p)->target.fill({r.x, r.y, r.w, r.h},
                                                                      ui::Paint::solid(ui::Color::Black));
                                  return;
                                }
                                static_cast<Owner*>(p)->renderer.drawUiText(*c, clip, statusText(c->resource));
                              }};
              assert(renderer.drawUiScene(scene, &paint));
              if (display.pixels != expected) {
                std::fprintf(stderr, "status differs: orientation=%d touch=%d bold=%d mode=%d known=%d family=%d\n",
                             orientation, touch, bold, mode, known, family);
                return false;
              }
              maxCommands[family] = std::max(maxCommands[family], unsigned(scene.command_count));
              ++cases;
            }
  std::printf("%u frozen catalog status pixel cases match; max commands browser/plugin/hub %u/%u/%u\n", cases,
              maxCommands[2], maxCommands[3], maxCommands[4]);
  return true;
}

static bool paginationCases(GfxRenderer& renderer, HalDisplay& display) {
  static constexpr const freeink::Icon* icons[] = {&icon_page_first_32, &icon_page_prev_32, &icon_page_next_32,
                                                   &icon_page_last_32};
  unsigned cases = 0;
  for (int orientation = 0; orientation < 4; ++orientation)
    for (int height : {40, 48, 60})
      for (uint8_t links = 1; links < 16; ++links)
        for (int page : {0, 1, 32767, 32768, 65535, 65536, INT32_MAX}) {
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
          char pageText[32];
          snprintf(pageText, sizeof(pageText), "%d/%d", page, page);
          if (page > 0) {
            ui::TextStyle centered;
            centered.align = ui::TextAlign::Center;
            const int16_t line = renderer.getLineHeight(UI_10_FONT_ID);
            target.text(
                {static_cast<int16_t>(20 + zone), static_cast<int16_t>(y + 1 + (height - 1 - line) / 2), zone, line},
                pageText, centered);
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
          layout.page_current = layout.page_total = page;
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
          owner.pageText = pageText;
          XpUiPaint paint{&owner, nullptr, nullptr, [](void*, uint32_t, uint32_t g) -> uint8_t { return g == 1; },
                          [](void* p, const XpGlyphTarget*, const XpUiCommand* c, XpUiRect clip) {
                            if (c->resource == 0x8000000c) {
                              auto& owner = *static_cast<Owner*>(p);
                              owner.renderer.drawUiText(*c, clip, owner.pageText);
                              return;
                            }
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

static bool popupCases(GfxRenderer& renderer, HalDisplay& display) {
  unsigned cases = 0;
  for (int orientation = 0; orientation < 4; ++orientation)
    for (bool touch : {false, true})
      for (int selected = 0; selected < 3; ++selected)
        for (bool invert : {false, true}) {
          renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
          renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
          ui::GfxRendererTarget target(renderer, touch);
          target.setFont(1, UI_12_FONT_ID);
          Owner owner{renderer, target, {}};
          auto device = target.deviceContext();
          device.safeArea = {};
          ui::InteractionBuffer<32> originalHits;
          ui::Frame<32> frame(target, device, {}, originalHits);
          ui::DialogOption options[3];
          for (int i = 0; i < 3; i++) {
            options[i].label = statusText(0x80000019 + i);
            options[i].action = XP_UI_CATALOG_HUB_OPTION;
            options[i].value = i;
            options[i].state = i == selected ? ui::StateFocused : ui::StateNormal;
          }
          ui::OptionDialogProps props;
          props.title = statusText(0x80000018);
          props.options = options;
          props.optionCount = 3;
          props.verticalOptions = true;
          props.inputMask = ui::InputTouch;
          props.titleText.font = 1;
          props.titleText.bold = true;
          props.titleText.align = ui::TextAlign::Center;
          props.titleText.maxLines = 2;
          props.buttonText.font = 1;
          props.padding = {16, 16, 16, 16};
          props.gap = 8;
          if (invert) {
            props.buttonStyles = ui::defaultButtonStyles();
            props.buttonStyles.focused = props.buttonStyles.selected;
            ui::setStyleRadius(props.buttonStyles, 12);
          }
          props.styles = ui::defaultPopupStyles();
          props.styles.normal.border = ui::Paint::solid(ui::Color::Black);
          props.styles.normal.borderWidth = 2;
          props.styles.normal.radius = 6;
          props.buttonHeight = renderer.getLineHeight(UI_12_FONT_ID) + 8;
          const int16_t width = std::min(device.width * 3 / 4, device.width - 40);
          const int16_t height = ui::optionDialogHeight(target, props, width);
          const auto card = ui::centeredRect(device.screen(), {width, height});
          renderer.clearScreen();
          frame.hit(card, 13, 0, ui::InputTouch);
          ui::optionDialog(frame, card, props);
          const auto expected = display.pixels;
          XpUiCatalogState state{};
          xp_ui_catalog_init(&state, XP_UI_CATALOG_PLUGINS, -1, 0);
          state.popup = selected + 1;
          XpUiCatalogLayout layout{};
          layout.list.screen = {0, 0, device.width, device.height};
          layout.list.area = layout.list.screen;
          layout.list.font = UI_12_FONT_ID;
          layout.small_font = UI_10_FONT_ID;
          layout.list.selection = invert ? 0 : 1;
          layout.list.radius = 12;
          layout.popup_padding = 16;
          layout.popup_gap = 8;
          layout.popup_side_margin = 20;
          layout.selection_padding = 4;
          layout.popup_radius = 6;
          layout.popup_border = 2;
          layout.touch = touch;
          XpUiCatalogResources services{
              &owner,
              [](void*, uint16_t, XpUiCatalogRow*) -> uint8_t { return 0; },
              [](void*, uint32_t h) { return statusText(h); },
              [](void* p, const char* t, int32_t f, uint8_t b) -> int32_t {
                return static_cast<Owner*>(p)->renderer.getTextWidth(f, t, static_cast<EpdFontFamily::Style>(b));
              },
              [](void* p, const char* t, int32_t f, uint8_t b, uint8_t lines, int16_t w) -> int32_t {
                auto& target = static_cast<Owner*>(p)->target;
                target.setFont(0, f);
                ui::TextStyle style;
                style.font = 0;
                style.bold = b;
                style.maxLines = lines;
                return ui::measureWrappedText(target, t, style, w).height;
              },
              [](void* p, int32_t f) -> int32_t { return static_cast<Owner*>(p)->renderer.getLineHeight(f); },
              0,
              0};
          XpUiCommand commands[48];
          XpUiBuffer buffer{commands, nullptr, 48, 0};
          XpUiScene scene{};
          XpUiListState next{};
          XpUiCatalogHits hits{};
          assert(xp_ui_catalog_view(&state, &layout, &services, &buffer, &next, &scene, &hits) == 1);
          XpUiPaint paint{&owner, nullptr, nullptr, [](void*, uint32_t, uint32_t) -> uint8_t { return 1; },
                          [](void* p, const XpGlyphTarget*, const XpUiCommand* c, XpUiRect clip) {
                            if (c->resource == 0x80000002) return;
                            static_cast<Owner*>(p)->renderer.drawUiText(*c, clip, statusText(c->resource));
                          }};
          renderer.clearScreen();
          assert(renderer.drawUiScene(scene, &paint));
          if (display.pixels != expected) {
            std::fprintf(stderr, "popup differs: orientation=%d touch=%d selected=%d invert=%d\n", orientation, touch,
                         selected, invert);
            return false;
          }
          assert(hits.control_count == originalHits.count());
          for (unsigned i = 0; i < hits.control_count; i++) {
            const auto a = hits.controls[i].rect;
            const auto b = originalHits.data()[i].rect;
            assert(a.x == b.x && a.y == b.y && a.w == b.width && a.h == b.height);
          }
          // Original OptionPopup::handleInput routes the SDK table first, then
          // changes highlight on press or dismisses on a non-chrome release.
          for (int targetIndex = -2; targetIndex < static_cast<int>(originalHits.count()); ++targetIndex) {
            int16_t x = targetIndex == -2 ? -1 : 0, y = x;
            if (targetIndex >= 0) {
              const auto rect = originalHits.data()[targetIndex].rect;
              x = rect.x + 1;
              y = rect.y + 1;
            }
            bool open = true;
            int highlight = selected;
            state.popup = selected + 1;
            for (bool release : {false, true}) {
              ui::InputSnapshot input;
              input.touchX = x;
              input.touchY = y;
              input.touchPressed = !release;
              input.touchReleased = release;
              const auto event = originalHits.route(input);
              bool redraw = false;
              int chosen = -1;
              if (event && event.action == XP_UI_CATALOG_HUB_OPTION) {
                highlight = event.value;
                chosen = highlight;
                open = false;
                redraw = true;
              } else if (!(event && event.action == 13)) {
                if (release && x >= 0) {
                  open = false;
                  redraw = true;
                } else if (!release && originalHits.activeIndex() >= 0) {
                  const auto& hit = originalHits.data()[originalHits.activeIndex()];
                  if (hit.action == XP_UI_CATALOG_HUB_OPTION && highlight != hit.value) {
                    highlight = hit.value;
                    redraw = true;
                  }
                }
              }
              XpUiInput routed{};
              routed.touch_x = x;
              routed.touch_y = y;
              routed.touch_flags = release ? XP_UI_TOUCH_RELEASED : XP_UI_TOUCH_PRESSED;
              XpUiInteraction hit{};
              if (xp_ui_catalog_hit(&hits, x, y, &hit)) {
                routed.action = hit.action;
                routed.value = hit.value;
              }
              const auto effect = xp_ui_catalog_update(&state, &routed, 0, 0, 0);
              assert(state.popup == (open ? highlight + 1 : 0));
              assert(bool(effect.invalidate) == redraw);
              assert(effect.kind == (chosen >= 0 ? int(XP_UI_CATALOG_HUB_OPTION) : int(XP_UI_EFFECT_NONE)));
              if (chosen >= 0) assert(effect.value == chosen);
            }
          }
          ++cases;
        }
  std::printf("%u plugin popup pixel/hit/input cases match SDK\n", cases);
  return true;
}

static bool searchGeometryCases(GfxRenderer& renderer) {
  unsigned cases = 0;
  for (int orientation = 0; orientation < 4; ++orientation)
    for (int16_t top : {5, 10, 13})
      for (int16_t offset : {12, 20, 30})
        for (int16_t minimum : {44, 48, 60}) {
          renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
          ui::GfxRendererTarget target(renderer);
          auto device = target.deviceContext();
          ui::InteractionBuffer<32> hits;
          ui::Frame<32> frame(target, device, {}, hits);
          ui::HeaderProps props;
          props.trailingIcon = ui::bitmapFromIcon(icon_search_32);
          props.trailingAction = XP_UI_CATALOG_SEARCH;
          props.trailingSize = 48;
          props.actionOffsetY = offset;
          props.minTouchSize = minimum;
          const ui::Rect band{0, top, device.width, 84};
          ui::header(frame, band, props);
          // CatalogUiActivity::searchButtonRect measures this same descriptor
          // separately from painting. Compare it with the SDK's routed hit.
          const auto measured = ui::ensureMinTouchRect(
              {static_cast<int16_t>(band.right() - 12 - props.trailingSize),
               static_cast<int16_t>(band.y + 4 + props.actionOffsetY), props.trailingSize, props.trailingSize},
              minimum, {0, 0, device.width, device.height});
          assert(hits.count() == 1);
          const auto actual = hits.data()[0].rect;
          assert(actual.x == measured.x && actual.y == measured.y && actual.width == measured.width &&
                 actual.height == measured.height);
          ++cases;
        }
  std::printf("%u search header measurement/hit cases match SDK\n", cases);
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
  if (!searchGeometryCases(renderer) || !statusCases(renderer, display) || !paginationCases(renderer, display) ||
      !popupCases(renderer, display))
    return 1;
  unsigned cases = 0;
  unsigned maxCommands[7]{};
  for (bool fullHeight : {false, true})
    for (uint8_t kind : {0, 1, 2, 3, 4, 5, 6})
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
                row.subtitle = kind == 6    ? nullptr
                               : kind >= 3  ? "A long synthetic description of the observatory and its many shelves, "
                                              "tools and reading rooms. Every visitor can borrow a book and read here."
                               : i % 4 == 0 ? "Remote status"
                                            : nullptr;
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
                screen.setContentMarginFromScreen(kind >= XP_UI_CATALOG_BROWSER ? ui::Insets{53, 0, 20, 0}
                                                                                : ui::Insets{55, 5, 50, 5});
                if (kind < XP_UI_CATALOG_BROWSER) screen.spacer(8);
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
                if (kind == 3 || kind == 4) {
                  props.subtitleText = theme.smallText;
                  props.subtitleText.maxLines = kind == 3 ? 2 : 3;
                }
                screen.syncListViewport(nav, props, owner.rows.size());
                if (kind == 3)
                  props.subtitleText.maxLines = xp_plugin_subtitle(
                      screen.body().height, props.labelText.maxLines, target.lineHeight(props.labelText.font),
                      target.lineHeight(props.valueText.font), target.lineHeight(props.subtitleText.font),
                      props.rowPaddingY);
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
              xp_ui_catalog_init(&state, kind < 3 ? kind : XP_UI_CATALOG_PLUGINS, -1, 0);
              if (kind == 3) state.mode = XP_UI_CATALOG_PLUGIN_PICKER;
              if (kind == 5) state.plugin_flags = XP_UI_CATALOG_XML;
              if (kind == 6) state.mode = XP_UI_CATALOG_LIST_PICKER;
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
  std::printf(
      "%u catalog cases match SDK pixels and viewport feedback; max commands "
      "servers/editor/browser/picker/json/xml/lists %u/%u/%u/%u/%u/%u/%u\n",
      cases, maxCommands[0], maxCommands[1], maxCommands[2], maxCommands[3], maxCommands[4], maxCommands[5],
      maxCommands[6]);
}
