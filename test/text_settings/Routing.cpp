// Test leaves provide fonts, storage and display; routing/build methods are read from production.
#include <FreeInkApp.h>
#include <xp_ui_text_settings.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

#include "RustActivityAdapter.h"
#include "components/UiAppHelpers.h"
#define LOG_DBG(...) ((void)0)
namespace fui = freeink::ui;
static int failCapacity = 0, allocationCalls = 0;
template <class T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  ++allocationCalls;
  if (int(count) == failCapacity) return {};
  return std::unique_ptr<T>(new std::remove_extent_t<T>[count]);
}
// ARENA_OWNER
// ARENA_METHODS
struct Metrics {
  int previewHeightPercent = 35, previewPadding = 10, contentSidePadding = 10;
  int tabSpacing = 4, headerUnderlineSize = 1, tabPillFullSlot = 0;
};
struct UITheme {
  static UITheme& getInstance() {
    static UITheme t;
    return t;
  }
  const Metrics& getMetrics() {
    static Metrics m;
    return m;
  }
};
struct Target : fui::DrawTarget {
  fui::DeviceContext deviceContext() {
    fui::DeviceContext d;
    d.width = 528;
    d.height = 792;
    d.hasTouch = true;
    return d;
  }
  fui::Size measureText(fui::FontId, const char* s, fui::TextStyle) const override {
    return {int16_t(strlen(s) * 8), 20};
  }
  int16_t lineHeight(fui::FontId) const override { return 20; }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect, const char*, fui::TextStyle) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
};
Target makeUiTarget(GfxRenderer&) { return {}; }
void sampleReaderChromeTheme(GfxRenderer&, XpUiReaderTheme& t) {
  t = {};
  t.screen = {0, 0, 528, 792};
  t.safe = t.screen;
  t.touch = 1;
  t.row_min = 30;
  t.row_gap = 3;
  t.row_padding = 5;
  t.spacing = 8;
  t.header_height = 40;
  t.tab_height = 50;
  t.top_padding = 4;
}
XpUiTextValues sampleValues() { return {}; }
#define tr(x) "Preview"
class TextSettingsActivity : public RustActivityAdapter {
 public:
  struct Entry {
    std::string name;
  };
  std::vector<Entry> fonts_, sizes_;
  XpUiTextState state{}, rendering{}, next{};
  XpUiTextValues values{};
  XpUiTextLayout layout{};
  XpUiReaderHitLayout publishedHits{}, nextHits{};
  ReaderChromeCommands commands;
  Publication publication{};
  char rowValues[7][16]{}, caption[128]{};
  const char* rowText[7]{};
  int32_t renderingScroll = 0;
  bool touchActivated = false;
  bool sceneFailed = false;
  unsigned saves = 0;
  bool denseRows = true;
  TextSettingsActivity(GfxRenderer& r, MappedInputManager& i) : RustActivityAdapter("TextSettings", r, i) {
    xp_ui_text_init(&state, 0, 0, 0, 20, 20, 0);
  }
  void activateRow(int) {
    ++saves;
    requestUpdate();
  }
  XpUiReaderRows rows() {
    return {this,
            [](void*, uint16_t, uint16_t) -> uint8_t { return 1; },
            [](void* p, uint16_t i, XpUiReaderRow* r) -> uint8_t {
              *r = {};
              r->label = i * 4 + 1;
              if (static_cast<TextSettingsActivity*>(p)->denseRows) {
                r->value = i * 4 + 3;
                r->toggle = 1;
              }
              return 1;
            },
            [](void*, uint32_t, int32_t, uint8_t) -> int32_t { return 64; },
            [](void*, uint32_t, int32_t, uint8_t, uint8_t, int16_t) -> int32_t { return 20; },
            [](void*, int32_t) -> int32_t { return 20; },
            uint16_t(state.tab < 2    ? 20
                     : state.tab == 2 ? 7
                                      : 4),
            64,
            uint32_t(rendering.tab) + 1};
  }
  Publication* rustPublication() override { return &publication; }
  XpUiInterest rustInterest(uint32_t now) const override { return xp_ui_text_interest(&state, now); }
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;
  bool buildRustScene(XpUiScene&) override;
  void rustPublished() override;
  void onExit() override;
  void enterArena();
  void paint() {
    RenderLock l;
    render(std::move(l));
  }
};
// PRODUCTION_METHODS

// Frozen dc21f50f onTabAction body, routed by the pinned original SDK app.
struct Original {
  enum class Tab { Family, Size, Layout, Style };
  Tab tab_ = Tab::Family;
  fui::ListNav nav;
  struct {
    bool isActive() const { return false; }
  } optionPopup_;
  Target target;
  fui::FreeInkApp<> app{target, target.deviceContext()};
  unsigned haptics = 0, panels = 0;
  fui::ListNav& activeNav() { return nav; }
  void rebuildRowItems() {}
  void requestUpdate() { app.invalidate(); }
  void onTabAction(const int index) {
    if (optionPopup_.isActive()) return;
    if (tab_ != static_cast<Tab>(index)) {
      tab_ = static_cast<Tab>(index);
      rebuildRowItems();
      auto& n = activeNav();
      n.selected = 0;
      n.followOnBuild = true;
      requestUpdate();
    }
    app.clearTapFlash();
  }
  Original() {
    app.setScreen([](fui::Screen<32>& s, void*) {
      fui::TabItem tabs[4];
      const char* labels[] = {"Font", "Size", "Layout", "Style"};
      for (int n = 0; n < 4; ++n) {
        tabs[n].label = labels[n];
        tabs[n].value = n;
      }
      fui::TabBarProps p;
      p.tabs = tabs;
      p.count = 4;
      p.action = 2;
      fui::tabBar(s.frame(), {0, 0, 528, 50}, p);
    });
    app.on(
        2,
        [](const fui::ActionEvent& e, void* p) {
          auto& o = *static_cast<Original*>(p);
          ++o.haptics;
          o.onTabAction(e.value);
        },
        this);
    app.render({});
  }
  void tap() {
    fui::InputSnapshot i;
    i.touchX = 10;
    i.touchY = 10;
    i.touchPressed = true;
    app.route(i);
    i.touchPressed = false;
    i.touchReleased = true;
    app.route(i);
    assert(app.invalidated() && app.refreshHint() == fui::RefreshHint::Fast);
    app.render({});
    ++panels;
  }
};
void tap(TextSettingsActivity& a, MappedInputManager& i, XpUiRect r) {
  i = {};
  i.touchX = r.x + r.w / 2;
  i.touchY = r.y + r.h / 2;
  i.touchFlags = XP_UI_TOUCH_PRESSED;
  a.loop();
  i.touchFlags = XP_UI_TOUCH_RELEASED;
  a.loop();
  i = {};
}
int main() {
  {
    GfxRenderer renderer;
    MappedInputManager input;
    TextSettingsActivity a(renderer, input);
    a.enterArena();
    // Before the first build, retain the previous port's immediate button effects.
    auto expected = a.state;
    XpUiInput event{};
    event.released = XP_UI_CONFIRM;
    const auto effect = xp_ui_text_update(&expected, &event);
    input.released = 1u << unsigned(MappedInputManager::Button::Confirm);
    const auto requests = a.requests;
    a.loop();
    assert(memcmp(&expected, &a.state, sizeof(expected)) == 0);
    assert(a.requests == requests + effect.invalidate && !a.sceneFailed);
    a.state.tabs[a.state.tab].selected = 1;
    a.loop();
    assert(a.saves == 1 && !a.publication.ready);
    a.onExit();
  }

  {
    GfxRenderer renderer;
    MappedInputManager input;
    TextSettingsActivity a(renderer, input);
    failCapacity = 32;
    a.enterArena();
    assert(a.finished && !a.commands.ready() && a.requests == 0);
    failCapacity = 0;
  }
  for (int ring : {0, 1}) {
    GfxRenderer renderer;
    renderer.finishRefresh = true;
    MappedInputManager input;
    TextSettingsActivity a(renderer, input);
    assert(a.commands.reserve(32));
    a.state.tabs[0].selected = ring;
    a.paint();
    assert(a.publication.ready);
    const auto before = a.state;
    const int allocations = allocationCalls;
    const auto requests = a.requests, panels = renderer.panels, haptics = haptic_feedback::count;
    Original o;
    o.nav.selected = ring;
    o.tap();
    tap(a, input, a.publishedHits.controls[0].rect);
    assert(a.requests == requests + 1);
    a.paint();
    assert(allocationCalls == allocations);
    assert(renderer.lastMode == HalDisplay::FAST_REFRESH);
    assert(renderer.panels - panels == o.panels && haptic_feedback::count - haptics == o.haptics);
    assert(a.state.tab == before.tab && a.state.tabs[0].selected == ring && a.saves == 0);
    const auto oldSaves = a.saves;
    tap(a, input, a.publishedHits.first_row);
    assert(a.saves == oldSaves + 1);
    a.paint();
    const auto beforeTab = a.requests;
    tap(a, input, a.publishedHits.controls[1].rect);
    assert(a.state.tab == 1 && a.state.tabs[1].selected == 0 && a.requests == beforeTab + 1);
    a.paint();
    const auto beforeScroll = a.requests;
    input.swipe = MappedInputManager::SwipeDir::Up;
    a.loop();
    input = {};
    assert(a.requests == beforeScroll + 1);
    a.paint();
    a.onExit();
    assert(!a.commands.ready());
  }
  for (bool painted : {false, true}) {
    GfxRenderer renderer;
    renderer.finishRefresh = true;
    MappedInputManager input;
    TextSettingsActivity a(renderer, input);
    assert(a.commands.reserve(32));
    if (painted) {
      a.denseRows = false;
      a.paint();
      assert(a.publication.ready && a.commands.buffer().command_capacity == 32);
      a.denseRows = true;
    }
    const auto panels = renderer.panels;
    failCapacity = 48;
    a.paint();
    assert(!a.publication.ready && !a.commands.ready() && renderer.panels == panels);
    a.state.tabs[0].selected = 1;
    const auto before = a.state;
    const auto oldHaptics = haptic_feedback::count;
    if (painted) tap(a, input, a.publishedHits.first_row);
    assert(a.saves == 0 && haptic_feedback::count == oldHaptics);
    input.released = 1u << unsigned(MappedInputManager::Button::Confirm);
    a.loop();
    assert(a.saves == 0 && a.requests > 0);
    assert(memcmp(&before, &a.state, sizeof(before)) == 0);
    a.paint();
    assert(!a.publication.ready && renderer.panels == panels);
    failCapacity = 0;
    input = {};
    input.pressed = 1u << unsigned(MappedInputManager::Button::NavNext);
    a.loop();
    assert(a.state.tabs[0].selected == before.tabs[0].selected);
    a.paint();
    assert(a.publication.ready && renderer.panels == panels + 1);
    input = {};
    input.released = 1u << unsigned(MappedInputManager::Button::Confirm);
    a.loop();
    assert(a.saves == 1);
    a.commands.reset();
    assert(a.commands.reserve(32));
    failCapacity = 48;
    a.paint();
    input = {};
    input.released = 1u << unsigned(MappedInputManager::Button::Back);
    a.loop();
    assert(a.finished);
    failCapacity = 0;
    a.onExit();
    assert(!a.commands.ready());
  }
  puts("routed active-tab parity and arena failure/recovery passed");
}
