#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <fontIds.h>
#include <xp_ui_network.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <optional>
#include <string>
#include <vector>

#include "Trace.h"
void textGoldenGlyph(const EpdFontFamily&, uint32_t, int, int, EpdFontFamily::Style) {}
void textGoldenScaled(int, uint32_t, int, int, EpdFontFamily::Style) {}
#include "AllocationTrace.h"
uint32_t testNow;
struct RenderLock {};
struct Rect {
  int x, y, width, height;
};
struct Metrics {
  int topPadding = 12, headerHeight = 36, tabBarHeight = 40, verticalSpacing = 8, contentSidePadding = 16,
      progressBarHeight = 8;
} metrics;
struct UITheme {
  enum class TextVerticalAlignment { TOP, CENTER, BOTTOM };
  template <class... Args>
  static void drawCenteredText(Args&&...) {
    std::abort();
  }
  template <class... Args>
  static void drawCenteredWrappedText(Args&&...) {
    std::abort();
  }

  static UITheme& getInstance() {
    static UITheme t;
    return t;
  }
  const Metrics& getMetrics() { return metrics; }
};
#define tr(x) #x
std::vector<std::string> chrome;
std::string rectText(Rect r) {
  return std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.width) + "," +
         std::to_string(r.height);
}
struct {
  template <class... Args>
  void drawHelpText(Args&&...) {
    std::abort();
  }
  void drawHeader(GfxRenderer&, Rect r, const char* s, const char* = nullptr) {
    allocations::Pause pause;
    chrome.push_back("header:" + rectText(r) + ":" + s);
  }
  void drawSubHeader(GfxRenderer&, Rect r, const char* a, const char* b = nullptr) {
    allocations::Pause pause;
    chrome.push_back("sub:" + rectText(r) + ":" + a + ":" + (b ? b : ""));
  }
  void drawProgressBar(GfxRenderer&, Rect r, size_t a, size_t b) {
    allocations::Pause pause;
    chrome.push_back("progress:" + rectText(r) + ":" + std::to_string(a) + ":" + std::to_string(b));
  }
  void drawButtonHints(GfxRenderer&, const char* a, const char* b, const char* c, const char* d) {
    allocations::Pause pause;
    chrome.push_back(std::string("hints:") + a + ":" + b + ":" + c + ":" + d);
  }
} GUI;
struct MappedInputManager {
  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) const { return {a, b, c, d}; }
};
#include <Logging.h>
#include <Memory.h>
#include <qrcode.h>
#include <xp_ffi.h>
constexpr int WL_CONNECTED = 3;
struct {
  int connected = 0;
  int status() { return connected; }
} WiFi;
constexpr const char* AP_HOSTNAME = "crosspoint";
constexpr int QR_CODE_WIDTH = 198, QR_CODE_HEIGHT = 198;
namespace QrUtils {
bool encodeQrBitmap(const std::string&, uint8_t*, size_t, uint8_t&);
void drawQrCode(const GfxRenderer&, const Rect&, const std::string&);
}  // namespace QrUtils
// QR_LEAF

enum class WebServerActivityState { MODE_SELECTION, WIFI_SELECTION, AP_STARTING, SERVER_RUNNING, SHUTTING_DOWN };
struct Original {
  GfxRenderer& renderer;
  MappedInputManager mappedInput;
  WebServerActivityState state = WebServerActivityState::MODE_SELECTION;
  bool isApMode = false;
  int consecutiveDisconnects = 0, lastWifiBars = 0;
  std::string connectedIP = "10.2.3.4", connectedSSID = "Oracle wireless network";
  void render(RenderLock&&);
  void renderServerRunning() const;
  void renderWifiIndicator(int) const;
};
struct CrossPointWebServerActivity {
  GfxRenderer& renderer;
  MappedInputManager mappedInput;
  XpUiWebState model{}, renderingState{};
  XpUiCommand commands[16]{};
  std::string connectedIP = "10.2.3.4", connectedSSID = "Oracle wireless network";
  std::optional<std::string> wifiConfig, primaryUrl, alternateUrl;
  struct ModelLock {
    explicit ModelLock(CrossPointWebServerActivity&) {}
  };
  bool buildRustScene(XpUiScene&);
  XpUiPaint rustResources();
};
// SHARED_BINDING
// ORIGINAL_RENDER
// PRODUCTION_METHODS
int main() {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  static EpdFont small(&ubuntu_10_regular), bold(&ubuntu_10_bold);
  renderer.insertFont(UI_10_FONT_ID, EpdFontFamily(&small, &bold));
  renderer.insertFont(SMALL_FONT_ID, EpdFontFamily(&small, &bold));
  FontDecompressor decompressor;
  assert(decompressor.init());
  FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
  unsigned cases = 0;
  size_t peak = 0;
  chrome.reserve(8);
  for (int orientation = 0; orientation < 4; ++orientation)
    for (int phase = 0; phase < 5; ++phase)
      for (bool ap : {false, true})
        for (int signal = -1; signal < 5; ++signal)
          for (int spacing : {8, 12}) {
            renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
            renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
            metrics.verticalSpacing = spacing;
            WiFi.connected = signal < 0 ? 0 : WL_CONNECTED;
            Original original{renderer};
            original.state = static_cast<WebServerActivityState>(phase);
            original.isApMode = ap;
            original.lastWifiBars = std::max(0, signal);
            original.consecutiveDisconnects = signal < 0 ? 2 : 0;
            renderer.clearScreen();
            chrome.clear();
            original.render(RenderLock{});
            renderer.clearScreen();
            chrome.clear();
            allocations::begin();
            original.render(RenderLock{});
            allocations::enabled = false;
            const auto expected = display.pixels;
            const auto expectedChrome = chrome;
            const auto expectedEvents = allocations::events;
            const auto expectedCount = allocations::count;
            const auto expectedPeak = allocations::peak;
            assert(allocations::live == 0);
            peak = std::max(peak, expectedPeak);
            CrossPointWebServerActivity candidate{renderer};
            candidate.model.phase = phase;
            candidate.model.is_ap = ap;
            candidate.model.bars = original.lastWifiBars;
            candidate.model.disconnects = original.consecutiveDisconnects;
            renderer.clearScreen();
            chrome.clear();
            allocations::begin();
            XpUiScene scene{};
            assert(candidate.buildRustScene(scene));
            const auto paint = candidate.rustResources();
            assert(renderer.drawUiScene(scene, &paint));
            allocations::enabled = false;
            assert(allocations::live == 0 && allocations::peak == expectedPeak && allocations::count == expectedCount);
            for (size_t i = 0; i < expectedCount; ++i) assert(allocations::events[i] == expectedEvents[i]);
            if (display.pixels != expected || chrome != expectedChrome) {
              fprintf(stderr, "Web mismatch o=%d phase=%d ap=%d bars=%d spacing=%d\n", orientation, phase, ap, signal,
                      spacing);
              return 1;
            }
            assert(scene.refresh == ((phase == 2 || phase == 3) ? XP_UI_FAST : XP_UI_NONE));
            ++cases;
          }

  for (int width : {480, 528, 792, 800}) {
    XpUiWebState state{};
    state.phase = XP_UI_WEB_RUNNING;
    XpUiWebLayout layout{
        {0, 0, static_cast<int16_t>(width), 800}, UI_10_FONT_ID, SMALL_FONT_ID, 12, 36, 40, 8, 16, 20, 1, {}};
    std::array<XpUiCommand, 16> commands{};
    XpUiBuffer buffer{commands.data(), nullptr, 2, 0};
    XpUiScene scene{};
    scene.generation = 42;
    const auto before = scene;
    assert(!xp_ui_network_web_view(&state, &layout, &buffer, &scene));
    assert(std::memcmp(&scene, &before, sizeof(scene)) == 0);
    buffer.command_capacity = 16;
    assert(xp_ui_network_web_view(&state, &layout, &buffer, &scene));
    assert(scene.command_count == 15);
  }
  printf("%u Web body/QR pixel, chrome and allocation cases passed; peak=%zu bytes\n", cases, peak);
}
