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
#include <optional>
#include <string>
#include <vector>

#include "Trace.h"
void textGoldenGlyph(const EpdFontFamily&, uint32_t, int, int, EpdFontFamily::Style) {}
void textGoldenScaled(int, uint32_t, int, int, EpdFontFamily::Style) {}
#include "AllocationTrace.h"
namespace fui = freeink::ui;
using UiScreen = fui::Screen<24>;
struct Rect {
  int x, y, width, height;
};
struct ThemeMetrics {
  int topPadding = 12, headerHeight = 36, tabBarHeight = 40, verticalSpacing = 8;
  int popupFrameThickness = 2, popupCornerRadius = 12;
} metrics;
Rect safe;
struct UITheme {
  static UITheme& getInstance() {
    static UITheme t;
    return t;
  }
  const ThemeMetrics& getMetrics() { return metrics; }
  Rect getScreenSafeArea(GfxRenderer&, bool, bool) { return safe; }
};
#define tr(x) #x
constexpr auto ACTION_ROW = 1, ACTION_SCAN = 2, ACTION_PROMPT = 3;
enum class WifiSelectionState {
  AUTO_CONNECTING,
  SCANNING,
  NETWORK_LIST,
  HIDDEN_SSID_ENTRY,
  PASSWORD_ENTRY,
  CONNECTING,
  CONNECTED,
  SAVE_PROMPT,
  CONNECTION_FAILED,
  FORGET_PROMPT
};
struct WifiSelectionActivity {
  GfxRenderer& renderer;
  struct {
    bool touch;
    bool hasTouch() { return touch; }
  } mappedInput;
  WifiSelectionState state = WifiSelectionState::NETWORK_LIST;
  std::vector<int> networks;
  std::vector<fui::ListItem> networkRowItems;
  std::string selectedSSID;
  size_t selectedNetworkIndex = 0;
  int savePromptSelection = 0, forgetPromptSelection = 0;
  fui::ListNav listNav;
  void buildListScreen(UiScreen&);
  void buildPromptDialog(UiScreen&);
};
struct Owner {
  GfxRenderer& renderer;
  fui::GfxRendererTarget& target;
  WifiSelectionActivity& original;
  std::optional<std::string> prompt;
  unsigned rowCalls = 0;
  const char* text(uint32_t h) {
    switch (h) {
      case 0x10000a:
        return tr(STR_NO_NETWORKS);
      case 0x10000b:
        return tr(STR_RETRY);
      case 0x10001e:
      case 0x100014:
        return tr(STR_CONNECTED);
      case 0x10001f:
      case 0x100017:
        return tr(STR_FORGET_NETWORK);
      case 0x100018:
        return prompt->c_str();
      case 0x100019:
        return tr(STR_FORGET_AND_REMOVE);
      case 0x10001a:
        return tr(STR_SAVE_PASSWORD);
      case 0x10001b:
        return tr(STR_CANCEL);
      case 0x10001c:
        return tr(STR_YES);
      case 0x10001d:
        return tr(STR_NO);
    }
    if (h > 0 && h < 0x100000) {
      auto& row = original.networkRowItems[(h - 1) / 4];
      return (h - 1) % 4 == 0 ? row.label : row.value;
    }
    return nullptr;
  }
};
// ORIGINAL_BODY
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
  unsigned cases = 0, maxCommands = 0, maxBudget = 0;
  const char* statuses[] = {"||||",   " |||",   "  ||",   "   |",   "+ ||||",   "+  |||",   "+   ||",   "+    |",
                            "* ||||", "*  |||", "*   ||", "*    |", "+ * ||||", "+ *  |||", "+ *   ||", "+ *    |"};
  uint64_t digest = 1469598103934665603ull;
  for (int orientation = 0; orientation < 4; ++orientation)
    for (int inset : {0, 7})
      for (bool touch : {false, true})
        for (int phase : {2, 7, 9})
          for (int style = 0; style < 4; ++style)
            for (int count : {0, 1, 17, 60})
              for (int selection : {0, 1})
                for (int length : {9, 33, 64, 255})
                  for (bool wide : {false, true})
                    for (bool heldTouch : {false, true})
                      for (int frameOrdinal : {0, 1}) {
                        if (heldTouch && !(touch && phase == 2 && count == 60 && selection == 1)) continue;
                        renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
                        renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
                        safe = {3, 5, renderer.getScreenWidth() - 8, renderer.getScreenHeight() - 45};
                        fui::GfxRendererTarget target(renderer, touch);
                        target.setFont(0, UI_10_FONT_ID);
                        target.setFont(1, UI_12_FONT_ID);
                        auto device = target.deviceContext();
                        device.safeArea = {static_cast<int16_t>(inset), static_cast<int16_t>(inset),
                                           static_cast<int16_t>(inset), static_cast<int16_t>(inset)};
                        auto theme = fui::themeTokensForLineHeight(renderer.getLineHeight(UI_12_FONT_ID));
                        theme.listSelectionStyle = static_cast<fui::SelectionStyle>(style);
                        theme.listRowGap = 3;
                        theme.listInset = 5;
                        theme.listRowRadius = 12;
                        WifiSelectionActivity original{renderer, {touch}};
                        original.state = static_cast<WifiSelectionState>(phase);
                        original.selectedSSID = std::string(length, 'w');
                        original.networks.resize(count);
                        original.networkRowItems.resize(count);
                        std::vector<std::string> labels;
                        labels.reserve(count);
                        for (int i = 0; i < count; ++i)
                          labels.push_back(std::string(wide ? 32 : 9, wide ? 'W' : 'a' + i % 20));
                        for (int i = 0; i < count; ++i) {
                          auto& row = original.networkRowItems[i];
                          row.label = labels[i].c_str();
                          row.value = statuses[i % 16];
                          row.actionValue = i;
                        }
                        original.selectedNetworkIndex = selection ? std::max(0, count - 1) : 0;
                        original.savePromptSelection = original.forgetPromptSelection = selection;
                        const auto requestedSelection = original.selectedNetworkIndex;
                        if (heldTouch) original.selectedNetworkIndex = 0;
                        original.listNav.reset(original.selectedNetworkIndex);
                        fui::InteractionBuffer<24> originalHits;
                        auto draw = [&] {
                          originalHits.beginPublishCycle();
                          fui::Frame<24> frame(target, device, {}, originalHits);
                          UiScreen screen(frame, theme);
                          renderer.clearScreen();
                          original.buildListScreen(screen);
                          originalHits.publish();
                        };
                        draw();
                        if (heldTouch) {
                          const auto& r = originalHits.publishedData()[0].rect;
                          fui::InputSnapshot press{};
                          press.touchPressed = true;
                          press.touchX = r.x + 3;
                          press.touchY = r.y + 3;
                          originalHits.routePublished(press);
                          assert(originalHits.activeIndex() == 0);
                          original.selectedNetworkIndex = requestedSelection;
                          original.listNav.selected = requestedSelection;
                          original.listNav.follow(count);
                        } else
                          original.listNav.reset(original.selectedNetworkIndex);
                        XpUiListState initialView{};
                        xp_ui_settings_list_init(&initialView, original.selectedNetworkIndex);
                        initialView.top = original.listNav.top;
                        initialView.visible = original.listNav.visibleRows;
                        initialView.drawn = original.listNav.drawnRows;
                        initialView.drawn_count = original.listNav.drawnCount;
                        if (heldTouch) initialView.active = 0;
                        for (int n = 0; n < frameOrdinal; ++n) draw();
                        allocations::begin();
                        draw();
                        allocations::enabled = false;
                        assert(allocations::live == 0);
                        const auto expected = display.pixels;
                        Owner owner{renderer, target, original, {}};
                        XpUiWifiState state{};
                        state.list = initialView;
                        state.flash_index = -1;
                        if (heldTouch) state.list.active = 0;
                        state.phase = phase;
                        state.count = count;
                        state.save_selection = state.forget_selection = selection;
                        XpUiWifiLayout layout{};
                        auto& b = layout.base;
                        b.list.screen = {0, 0, device.width, device.height};
                        b.safe = {static_cast<int16_t>(safe.x), static_cast<int16_t>(safe.y),
                                  static_cast<int16_t>(safe.width), static_cast<int16_t>(safe.height)};
                        const auto ds = device.safeRect();
                        b.list.area = {ds.x, ds.y, ds.width, ds.height};
                        b.list.font = UI_12_FONT_ID;
                        b.small_font = UI_10_FONT_ID;
                        b.list.row_height = theme.listMinRowHeight;
                        b.list.row_gap = theme.listRowGap;
                        b.list.inset = theme.listInset;
                        b.list.padding = theme.listSidePadding;
                        b.list.scroll_width = theme.listScrollWidth;
                        b.list.radius = theme.listRowRadius;
                        b.list.selection = style;
                        b.list.scroll_left = theme.listScrollSide;
                        b.list.bold = theme.bodyText.bold;
                        b.top_padding = metrics.topPadding;
                        b.header_height = metrics.headerHeight;
                        b.tab_height = metrics.tabBarHeight;
                        b.spacing = metrics.verticalSpacing;
                        b.touch = touch;
                        b.preview_height = theme.rowHeight;
                        b.fixed_font = b.help_font = UI_10_FONT_ID;
                        layout.popup_border = metrics.popupFrameThickness;
                        layout.popup_radius = metrics.popupCornerRadius;
                        layout.legacy_body_font = UI_12_FONT_ID;
                        XpUiWifiViewServices services{
                            {&owner,
                             [](void* ctx, uint16_t i, XpUiSettingsRow* row) -> uint8_t {
                               auto& o = *static_cast<Owner*>(ctx);
                               ++o.rowCalls;
                               auto& r = o.original.networkRowItems[i];
                               *row = {r.label, r.subtitle, r.value, 0, 0, 0, 1, 0};
                               return 1;
                             },
                             [](void* ctx, const char* text, int32_t font, uint8_t bold) -> int32_t {
                               return static_cast<Owner*>(ctx)->renderer.getTextWidth(
                                   font, text, static_cast<EpdFontFamily::Style>(bold));
                             },
                             [](void* ctx, const char* text, int32_t font, uint8_t bold, uint8_t lines,
                                int16_t width) -> int32_t {
                               auto& t = static_cast<Owner*>(ctx)->target;
                               t.setFont(3, font);
                               fui::TextStyle style;
                               style.font = 3;
                               style.bold = bold;
                               style.maxLines = lines;
                               return fui::measureWrappedText(t, text, style, width).height;
                             },
                             [](void* ctx, int32_t font) -> int32_t {
                               return static_cast<Owner*>(ctx)->renderer.getLineHeight(font);
                             },
                             static_cast<uint16_t>(count), 0},
                            [](void* ctx, uint32_t h) -> const char* { return static_cast<Owner*>(ctx)->text(h); }};
                        if (phase == 7 || phase == 9)
                          owner.prompt.emplace(std::string(tr(STR_NETWORK_PREFIX)) + original.selectedSSID);
                        XpUiCommand commands[256]{};
                        XpUiBuffer buffer{commands, nullptr, 256, 0};
                        XpUiScene scene{};
                        XpUiListState next{};
                        XpUiWifiHits hits{};
                        XpUiWifiBudget budget{};
                        assert(
                            xp_ui_network_wifi_budget(&state, &layout, renderer.getLineHeight(UI_12_FONT_ID), &budget));
                        for (int n = 0; n <= frameOrdinal; ++n) {
                          owner.rowCalls = 0;
                          assert(xp_ui_network_wifi_view(&state, &layout, &services, &buffer, &next, &scene, &hits) ==
                                 1);
                          if (heldTouch) {
                            const auto heldCalls = owner.rowCalls;
                            auto unpressed = state;
                            unpressed.list.active = -1;
                            owner.rowCalls = 0;
                            assert(xp_ui_network_wifi_view(&unpressed, &layout, &services, &buffer, &next, &scene,
                                                           &hits) == 1);
                            assert(heldCalls == 2 * owner.rowCalls);  // At most one re-emission of the same layout.
                            assert(xp_ui_network_wifi_view(&state, &layout, &services, &buffer, &next, &scene, &hits) ==
                                   1);
                          }
                          auto painted = state;
                          xp_ui_network_wifi_view_publish(&state, &painted, &next);
                        }
                        assert(scene.command_count <= budget.commands);
                        maxCommands = std::max<unsigned>(maxCommands, scene.command_count);
                        maxBudget = std::max<unsigned>(maxBudget, budget.commands);
                        assert(hits.rows.count <= budget.rows);
                        for (size_t i = 0; i < originalHits.publishedCount(); ++i) {
                          const auto& originalHit = originalHits.publishedData()[i];
                          XpUiInput input{};
                          input.touch_x = originalHit.rect.x + originalHit.rect.width / 2;
                          input.touch_y = originalHit.rect.y + originalHit.rect.height / 2;
                          input.touch_flags = XP_UI_TOUCH_PRESSED;
                          assert(xp_ui_network_wifi_hit(&hits, &state, &input));
                          assert(input.action == originalHit.action &&
                                 input.value == (input.action == 1 ? static_cast<int>(i) : originalHit.value));
                          input.touch_flags = XP_UI_TOUCH_RELEASED;
                          assert(xp_ui_network_wifi_hit(&hits, &state, &input));
                          assert(input.action == originalHit.action && input.value == originalHit.value);
                        }
                        XpUiPaint paint{
                            &owner, nullptr, nullptr, [](void*, uint32_t, uint32_t g) -> uint8_t { return g == 1; },
                            [](void* ctx, const XpGlyphTarget*, const XpUiCommand* cmd, XpUiRect clip) {
                              auto& o = *static_cast<Owner*>(ctx);
                              if (cmd->kind != XP_UI_TEXT_HANDLE || cmd->resource == XP_UI_WIFI_PRESS_SCAN) return;
                              if (cmd->resource == XP_UI_WIFI_PROMPT_SSID ||
                                  cmd->resource == XP_UI_WIFI_PROMPT_CONNECTED ||
                                  cmd->resource == XP_UI_WIFI_PROMPT_FORGET) {
                                o.target.setFont(3, cmd->font);
                                fui::TextStyle style;
                                style.font = 3;
                                style.bold = cmd->style & 1;
                                style.maxLines = cmd->max_lines;
                                style.align = static_cast<fui::TextAlign>(cmd->align);
                                const auto& r = cmd->rect;
                                auto lineCommand = *cmd;
                                lineCommand.max_lines = 1;
                                lineCommand.align = 0;
                                fui::layoutText(o.target, {r.x, r.y, r.w, r.h}, o.text(cmd->resource), style,
                                                [&](const char* line, fui::Rect bounds) {
                                                  lineCommand.rect = {bounds.x, bounds.y, bounds.width, bounds.height};
                                                  o.renderer.drawUiText(lineCommand, clip, line);
                                                });
                              } else
                                o.renderer.drawUiText(*cmd, clip, o.text(cmd->resource));
                            }};
                        assert(renderer.drawUiScene(scene, &paint));
                        if (display.pixels != expected) {
                          fprintf(stderr, "held=%d wide=%d active=%d original=%d first=%d\n", heldTouch, wide,
                                  state.list.active, originalHits.activeIndex(), originalHits.publishedData()[0].value);
                          fprintf(stderr,
                                  "WiFi pixels differ o=%d touch=%d phase=%d style=%d count=%d selection=%d length=%d "
                                  "top=%d/%d visible=%d/%d\n",
                                  orientation, touch, phase, style, count, selection, length, next.top,
                                  original.listNav.top, next.visible, original.listNav.visibleRows);
                          return 1;
                        }
                        for (auto b : display.pixels) {
                          digest ^= b;
                          digest *= 1099511628211ull;
                        }
                        ++cases;
                      }
  printf("%u WiFi body pixel cases %016llx; max commands %u, max bound %u\n", cases, (unsigned long long)digest,
         maxCommands, maxBudget);
}
