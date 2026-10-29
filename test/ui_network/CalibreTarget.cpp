#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <fontIds.h>
#include <xp_ui_network.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
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
  void drawHeader(GfxRenderer&, Rect r, const char* s, const char* = nullptr) {
    allocations::Pause pause;
    chrome.push_back("header:" + rectText(r) + ":" + s);
  }
  void drawSubHeader(GfxRenderer&, Rect r, const char* a, const char* b) {
    allocations::Pause pause;
    chrome.push_back("sub:" + rectText(r) + ":" + a + ":" + b);
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
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) { return {a, b, c, d}; }
};
enum class CalibreConnectState { WIFI_SELECTION, SERVER_STARTING, SERVER_RUNNING, ERROR };
struct Original {
  GfxRenderer& renderer;
  MappedInputManager mappedInput;
  CalibreConnectState state = CalibreConnectState::WIFI_SELECTION;
  std::string connectedIP = "10.2.3.4", connectedSSID = "Long network name";
  size_t lastProgressReceived = 0, lastProgressTotal = 0;
  std::string currentUploadName, lastCompleteName;
  uint32_t lastCompleteAt = 0;
  void render(RenderLock&&);
};
struct CalibreConnectActivity {
  GfxRenderer& renderer;
  MappedInputManager mappedInput;
  XpUiCalibreState model{}, renderingState{};
  XpUiCommand commands[14]{};
  std::string connectedIP = "10.2.3.4", connectedSSID = "Long network name", currentUploadName, lastCompleteName;
  std::optional<std::string> paintingUploadLabel;
  struct ModelLock {
    explicit ModelLock(CalibreConnectActivity&) {}
  };
  static const char* text(uint32_t);
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
  static EpdFont small(&ubuntu_10_regular), bold(&ubuntu_10_bold), medium(&ubuntu_12_regular),
      mediumBold(&ubuntu_12_bold);
  renderer.insertFont(UI_10_FONT_ID, EpdFontFamily(&small, &bold));
  renderer.insertFont(SMALL_FONT_ID, EpdFontFamily(&small, &bold));
  renderer.insertFont(UI_12_FONT_ID, EpdFontFamily(&medium, &mediumBold));
  FontDecompressor decompressor;
  assert(decompressor.init());
  FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
  unsigned cases = 0;
  chrome.reserve(8);
  for (int orientation = 0; orientation < 4; ++orientation)
    for (int phase = 0; phase < 4; ++phase)
      for (int progress = 0; progress < 4; ++progress)
        for (uint32_t age : {5999u, 6000u, 6001u})
          for (bool wrapped : {false, true})
            for (size_t length : {size_t(0), size_t(9), size_t(260)}) {
              renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
              renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
              testNow = (wrapped ? 0xfffffff0u : 100u) + age;
              Original original{renderer};
              original.state = static_cast<CalibreConnectState>(phase);
              original.lastProgressTotal = progress == 0 ? 0 : 100;
              original.lastProgressReceived = progress == 2 ? 101 : progress == 3 ? 100 : 42;
              original.lastCompleteAt = wrapped ? 0xfffffff0u : 100u;
              original.currentUploadName = original.lastCompleteName = std::string(length, 'b');
              chrome.clear();
              original.render(RenderLock{});
              chrome.clear();
              allocations::begin();
              original.render(RenderLock{});
              allocations::enabled = false;
              const auto expectedAllocations = allocations::events;
              const auto expectedAllocationCount = allocations::count;
              const auto expectedPeak = allocations::peak;
              assert(allocations::live == 0);
              const auto expected = display.pixels;
              const auto expectedChrome = chrome;
              CalibreConnectActivity candidate{renderer};
              candidate.model.phase = phase;
              candidate.model.received = original.lastProgressReceived;
              candidate.model.total = original.lastProgressTotal;
              candidate.model.completed_at = original.lastCompleteAt;
              candidate.currentUploadName = original.currentUploadName;
              candidate.lastCompleteName = original.lastCompleteName;
              XpUiScene scene{};
              chrome.clear();
              allocations::begin();
              assert(candidate.buildRustScene(scene));
              const auto paint = candidate.rustResources();
              assert(renderer.drawUiScene(scene, &paint));
              allocations::enabled = false;
              assert(allocations::live == 0 && allocations::peak == expectedPeak);
              assert(allocations::count == expectedAllocationCount);
              for (size_t i = 0; i < allocations::count; ++i) assert(allocations::events[i] == expectedAllocations[i]);
              if (display.pixels != expected || chrome != expectedChrome) {
                fprintf(stderr, "Calibre mismatch o=%d phase=%d progress=%d age=%u wrap=%d length=%zu\n", orientation,
                        phase, progress, age, wrapped, length);
                return 1;
              }
              ++cases;
            }
  for (int width : {480, 528, 792, 800}) {
    XpUiCalibreState state{};
    state.phase = XP_UI_CALIBRE_RUNNING;
    state.received = 42;
    state.total = 100;
    state.completed_at = 0xfffffff0u;
    XpUiCalibreLayout layout{{0, 0, static_cast<int16_t>(width), 800},
                             UI_10_FONT_ID,
                             UI_12_FONT_ID,
                             SMALL_FONT_ID,
                             12,
                             36,
                             40,
                             8,
                             16,
                             8,
                             20,
                             24,
                             0,
                             0};
    XpUiCommand commands[14]{};
    XpUiBuffer smallBuffer{commands, nullptr, 2, 0};
    XpUiScene scene{};
    scene.generation = 0xfeed;
    assert(xp_ui_network_calibre_view(&state, &layout, 0xfffffff0u + 5999u, &smallBuffer, &scene) == 0);
    assert(scene.generation == 0xfeed);
    XpUiBuffer buffer{commands, nullptr, 14, 0};
    for (uint32_t age : {5999u, 6000u, 6001u}) {
      assert(xp_ui_network_calibre_view(&state, &layout, 0xfffffff0u + age, &buffer, &scene) == 1);
      bool completed = false;
      for (unsigned i = 0; i < scene.command_count; ++i)
        completed |= commands[i].resource == XP_UI_CALIBRE_COMPLETED_TEXT;
      assert(completed == (age < 6000));
    }
  }
  printf("%u Calibre body pixel/chrome-argument cases passed\n", cases);
}
