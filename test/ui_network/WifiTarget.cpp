#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <fontIds.h>

#include <cassert>
#include <cstdio>
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
  unsigned cases = 0;
  uint64_t digest = 1469598103934665603ull;
  for (int orientation = 0; orientation < 4; ++orientation)
    for (bool touch : {false, true})
      for (int phase : {2, 7, 9})
        for (int style = 0; style < 4; ++style)
          for (int count : {0, 1, 17, 60})
            for (int selection : {0, 1})
              for (int length : {9, 33, 64, 255}) {
                renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
                renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
                safe = {3, 5, renderer.getScreenWidth() - 8, renderer.getScreenHeight() - 45};
                fui::GfxRendererTarget target(renderer, touch);
                target.setFont(0, UI_10_FONT_ID);
                target.setFont(1, UI_12_FONT_ID);
                auto device = target.deviceContext();
                device.safeArea = {};
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
                for (int i = 0; i < count; ++i) labels.push_back(std::string(i % 3 == 0 ? 32 : 9, 'a' + i % 20));
                for (int i = 0; i < count; ++i) {
                  auto& row = original.networkRowItems[i];
                  row.label = labels[i].c_str();
                  row.value = i % 2 ? "+ * ||||" : "  |";
                  row.actionValue = i;
                }
                original.selectedNetworkIndex = selection ? std::max(0, count - 1) : 0;
                original.savePromptSelection = original.forgetPromptSelection = selection;
                original.listNav.reset(original.selectedNetworkIndex);
                auto draw = [&] {
                  fui::InteractionBuffer<24> hits;
                  fui::Frame<24> frame(target, device, {}, hits);
                  UiScreen screen(frame, theme);
                  renderer.clearScreen();
                  original.buildListScreen(screen);
                };
                draw();
                allocations::begin();
                draw();
                allocations::enabled = false;
                assert(allocations::live == 0);
                for (auto b : display.pixels) {
                  digest ^= b;
                  digest *= 1099511628211ull;
                }
                ++cases;
              }
  printf("%u original WiFi body cases %016llx\n", cases, (unsigned long long)digest);
}
