#include <xp_ui_reader.h>
#include <xp_ui_reader_chrome.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <new>
#include <string>

#include "RustActivityAdapter.h"
#include "components/UiAppHelpers.h"

void* operator new(size_t n) {
  assert(criticalDepth == 0);
  if (auto* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
  assert(criticalDepth == 0);
  std::free(p);
}

// Only hardware drawing and resource metrics are stubbed; publication, routing,
// command growth, parent policy and the failed-scene gate are production code.
namespace freeink::ui {
struct Rect {
  int16_t x = 0, y = 0, width = 528, height = 792;
};
struct BitmapRef {};
struct ListItem {
  bool toggle = false, toggleChecked = false;
};
struct ListNav {
  int selected = 0, top = 0, visibleRows = 1, drawnRows = 0, drawnCount = 0;
  void reset() { *this = {}; }
  int pageRows() const { return drawnRows > 0 ? drawnRows : visibleRows; }
};
enum class BitmapMode { Center };
struct TextStyle {
  int font = 0;
  uint8_t bold = 0, maxLines = 1;
};
struct GfxRendererTarget {
  struct Device {
    Rect safeRect() const { return {}; }
  };
  Device deviceContext() const { return {}; }
  void setFont(int, int) {}
  void bitmap(Rect, BitmapRef, BitmapMode) {}
};
inline Rect measureWrappedText(GfxRendererTarget&, const char*, TextStyle, int16_t) { return {0, 0, 20, 20}; }
inline BitmapRef bitmapFromIcon(int) { return {}; }
}  // namespace freeink::ui
namespace fui = freeink::ui;
struct EpdFontFamily {
  enum Style { Regular };
};
struct UITheme {
  static UITheme& getInstance() {
    static UITheme t;
    return t;
  }
  struct Metrics {
    int listRowHeight = 48;
  };
  Metrics getMetrics() const { return {}; }
};
constexpr int icon_reader_contents_24 = 0, icon_reader_text_24 = 1, icon_reader_more_24 = 2, icon_reader_back_24 = 3,
              icon_reader_next_24 = 4;
static bool allocationFails = false;
static int failure = 0;
static unsigned failedAllocations = 0;
template <class T>
std::unique_ptr<T> makeUniqueNoThrow(size_t n) {
  if (allocationFails) {
    ++failedAllocations;
    return {};
  }
  return std::make_unique<T>(n);
}
void sampleReaderChromeTheme(const GfxRenderer&, XpUiReaderTheme& t) {
  t = {};
  t.screen = t.safe = {0, 0, 528, 792};
  t.body_font = 1;
  t.small_font = 0;
  t.title_font = 2;
  t.row_min = 48;
  t.row_padding = 8;
  t.scroll_width = 3;
  t.space_sm = 4;
  t.space_md = 8;
  t.space_lg = 16;
  t.control_radius = t.sheet_radius = 12;
}
static auto injectedView(const XpUiListState* state, const XpUiReaderToolbarModel* model, const XpUiReaderTheme* theme,
                         const XpUiReaderRows* rows, const XpUiBuffer* buffer, XpUiListState* next, XpUiScene* scene,
                         XpUiReaderHitLayout* hits) {
  if (failure == 1) return uint8_t(XP_UI_READER_VIEW_INVALID);
  if (failure == 3) {
    allocationFails = true;
    return uint8_t(XP_UI_READER_VIEW_CAPACITY);
  }
  return xp_ui_reader_toolbar_view(state, model, theme, rows, buffer, next, scene, hits);
}
// COMMAND_CLASS
// TOOLBAR_CLASS
ReaderToolbarUi::ReaderToolbarUi(GfxRenderer& r) : renderer(r) {}
// TOOLBAR_METHODS

class ReaderActivity : public RustActivityAdapter {
 public:
  using RustActivityAdapter::RustActivityAdapter;
  unsigned pageDrawn = 0;
  void render(RenderLock&&) override { ++pageDrawn; }
};
class EpubReaderActivity : public ReaderActivity {
 public:
  enum class Overlay { None, Toolbar, Contents, Text, More };
  Overlay overlay = Overlay::Toolbar;
  std::unique_ptr<ReaderToolbarUi> toolbarUi;
  ReaderToolbarUi::Routed routed{};
  unsigned actions = 0, notifications = 0, cancelled = 0;
  uint32_t fields[8]{XP_UI_READER_OVERLAY_MODE_TOOLBAR};
  EpubReaderActivity(GfxRenderer& r, MappedInputManager& i)
      : ReaderActivity("reader", r, i), toolbarUi(std::make_unique<ReaderToolbarUi>(r)) {
    toolbarUi->begin();
  }
  bool panelHoldJumped = false;
  bool recoverBeforeCancel = false, popupActive = false;
  unsigned popupInputs = 0, routes = 0, sampled = 0, swipes = 0;
  unsigned restored = 0, stored = 0, redrawn = 0, pushed = 0, settled = 0, lockDepth = 0;
  // FAILED_STATE
  void rustSceneFailed() override {
    ++notifications;
    failedState();
  }
  // CANCEL_DECL
  void closeOverlayToPage() {
    ++cancelled;
    ++actions;
    overlay = Overlay::None;
  }
  bool tryIdleLoop() { return false; }
  void loop() override;
  void render(RenderLock&&) override;
  bool runRecoveryPolicy(uint8_t) { return false; }
  bool rejected() const { return rustSceneRejected(); }
  void build() {
    auto& o = *this;
    ReaderToolbarUi::Model model;
    model.chapterTitle = "Chapter";
    model.pageInfo = "1/10";
    // PUBLICATION
  }
  bool rejectBeforeDispatch = false;
  void dispatchOverlayInput() {
    if (rejectBeforeDispatch) {
      rejectBeforeDispatch = false;
      build();
    }
    auto& o = *this;
    // OVERLAY_DISPATCH
  }
  void handleOverlayInput();
  void runOverlayPolicy(uint8_t operation) {
    const XpUiReaderOverlayServices services{
        this, [](void* p, uint8_t f) { return static_cast<EpubReaderActivity*>(p)->fields[f]; },
        [](void* p, uint8_t f, uint32_t v) { static_cast<EpubReaderActivity*>(p)->fields[f] = v; },
        [](void* p, uint8_t q) -> uint32_t {
          auto& o = *static_cast<EpubReaderActivity*>(p);
          switch (static_cast<XpUiReaderOverlayQuery>(q)) {
            case XP_UI_READER_OVERLAY_QUERY_HAS_TOOLBAR:
              return 1;
            case XP_UI_READER_OVERLAY_QUERY_POPUP_ACTIVE:
              return o.popupActive;
            case XP_UI_READER_OVERLAY_QUERY_TOC_COUNT:
            case XP_UI_READER_OVERLAY_QUERY_MORE_COUNT:
            case XP_UI_READER_OVERLAY_QUERY_ROWS:
              return 5;
            case XP_UI_READER_OVERLAY_QUERY_ROUTED:
              return o.routed.routed;
            case XP_UI_READER_OVERLAY_QUERY_VALUE:
              return o.routed.value;
            case XP_UI_READER_OVERLAY_QUERY_RELEASED:
              return (o.mappedInput.wasReleased(MappedInputManager::Button::Confirm) ? XP_UI_CONFIRM : 0) |
                     (o.mappedInput.wasReleased(MappedInputManager::Button::Back) ? XP_UI_BACK : 0);
            default:
              assert(false);
              return 0;
          }
        },
        [](void* p, uint8_t c, int32_t, int32_t) -> uint32_t {
          auto& o = *static_cast<EpubReaderActivity*>(p);
          switch (static_cast<XpUiReaderOverlayCommand>(c)) {
            case XP_UI_READER_OVERLAY_COMMAND_ROUTE:
              ++o.routes;
              o.routed = o.toolbarUi->route(o.mappedInput);
              return static_cast<uint32_t>(o.routed.event);
            case XP_UI_READER_OVERLAY_COMMAND_SAMPLE_BUTTONS:
              ++o.sampled;
              return 0;
            case XP_UI_READER_OVERLAY_COMMAND_TAKE_SWIPE:
              ++o.swipes;
              return static_cast<uint32_t>(o.mappedInput.wasSwipe());
            case XP_UI_READER_OVERLAY_COMMAND_POPUP_INPUT:
              ++o.popupInputs;
              if (o.mappedInput.touchFlags || o.mappedInput.released) ++o.actions;
              return 0;
            case XP_UI_READER_OVERLAY_COMMAND_DISMISS_POPUP:
              o.popupActive = false;
              return 0;
            case XP_UI_READER_OVERLAY_COMMAND_LOCK:
              ++o.lockDepth;
              return 0;
            case XP_UI_READER_OVERLAY_COMMAND_UNLOCK:
              --o.lockDepth;
              return 0;
            case XP_UI_READER_OVERLAY_COMMAND_SETTLE:
              ++o.settled;
              return 0;
            case XP_UI_READER_OVERLAY_COMMAND_RESTORE:
              ++o.restored;
              return 0;
            case XP_UI_READER_OVERLAY_COMMAND_STORE:
              ++o.stored;
              return 1;
            case XP_UI_READER_OVERLAY_COMMAND_RENDER:
              ++o.redrawn;
              return 0;
            case XP_UI_READER_OVERLAY_COMMAND_PUSH:
              ++o.pushed;
              return 0;
            case XP_UI_READER_OVERLAY_COMMAND_UPDATE:
              o.requestUpdate();
              return 0;
            case XP_UI_READER_OVERLAY_COMMAND_SCROLL:
              ++o.actions;
              return 1;
            case XP_UI_READER_OVERLAY_COMMAND_OPEN:
              ++o.actions;
              return 0;
            case XP_UI_READER_OVERLAY_COMMAND_CLOSE:
              o.closeOverlayToPage();
              return 0;
            default:
              assert(false);
              return 0;
          }
        }};
    xp_ui_reader_overlay_run(&services, operation);
  }
};
// LOOP_METHOD
// INPUT_METHOD
// CANCEL_METHOD
// PARENT_RENDER

int main(int argc, char** argv) {
  assert(argc == 4);
  const std::string stage = argv[1], kind = argv[2], check = argv[3];
  GfxRenderer r;
  MappedInputManager input;
  EpubReaderActivity a(r, input);
  const auto confirm = 1u << static_cast<unsigned>(MappedInputManager::Button::Confirm);
  if (check == "cancel-mixed") {
    a.build();
    const auto& hits = a.toolbarUi->published;
    bool found = false;
    for (unsigned i = 0; i < hits.control_count; ++i) {
      if (hits.controls[i].action == XP_UI_READER_OVERLAY_EVENT_TOOL) {
        input.touchX = hits.controls[i].rect.x + 1;
        input.touchY = hits.controls[i].rect.y + 1;
        found = true;
        break;
      }
    }
    assert(found);
    input.touchFlags = XP_UI_TOUCH_PRESSED | XP_UI_TOUCH_HELD;
    a.loop();
    const auto previous = a.actions;
    failure = 1;
    a.build();
    a.loop();  // Observe the held contact on the rejected scene.
    if (stage == "contents") a.fields[0] = XP_UI_READER_OVERLAY_MODE_CONTENTS;
    if (stage == "text" || stage == "popup") a.fields[0] = XP_UI_READER_OVERLAY_MODE_TEXT;
    if (stage == "more") a.fields[0] = XP_UI_READER_OVERLAY_MODE_MORE;
    a.popupActive = stage == "popup";
    a.fields[XP_UI_READER_OVERLAY_FIELD_STORED] = 1;
    a.routes = a.sampled = a.swipes = a.popupInputs = 0;
    a.recoverBeforeCancel = kind == "recover";
    input.touchFlags = XP_UI_TOUCH_RELEASED;
    input.released = confirm | (1u << static_cast<unsigned>(MappedInputManager::Button::Back));
    input.held = 1u << static_cast<unsigned>(MappedInputManager::Button::Down);
    input.swipe = MappedInputManager::SwipeDir::Up;
    a.loop();
    assert(a.routes == 0 && a.sampled == 0 && a.swipes == 0 && a.popupInputs == 0);
    assert(a.lockDepth == 0);
    if (stage == "toolbar") {
      assert(a.cancelled == 1 && a.actions == previous + 1);
    } else {
      assert(a.actions == previous && !a.popupActive);
      assert(a.fields[0] == (stage == "popup" ? XP_UI_READER_OVERLAY_MODE_TEXT : XP_UI_READER_OVERLAY_MODE_TOOLBAR));
      assert(a.restored == 1 && a.stored == 1 && a.settled >= 1);
      assert(a.redrawn == 1 && a.pushed == 1);
    }
    return 0;
  }
  if (check == "prepaint") {
    input.touchFlags = XP_UI_TOUCH_RELEASED;
    a.loop();
    assert(a.actions == 0);
    input = {};
    input.released = confirm;
    a.loop();
    assert(a.actions == 1);
    return 0;
  }
  if (stage != "first") a.build();
  // Track a real held gesture on the current scene before the failed rebuild.
  if (check == "held-button") {
    input.held = confirm;
    a.loop();
  }
  if (check == "held-touch") {
    input.touchFlags = XP_UI_TOUCH_PRESSED | XP_UI_TOUCH_HELD;
    a.loop();
  }
  if (check == "gesture-state") {
    a.panelHoldJumped = true;
    a.toolbarUi->contact = {1, 100, 1, {0, 0, 0}};
  }
  input = {};
  if (stage == "setting") {
    r.fontHeight = 28;
    a.toolbarUi->resetNavigation(0, 0);
  }
  if (kind == "reserve") {
    a.toolbarUi->commands.reset();
    allocationFails = true;
  }
  if (kind == "grow") failure = 3;
  if (kind == "builder") failure = 1;
  if (kind == "paint") r.paintOk = false;
  if (check == "dispatch") {
    const auto previous = a.actions;
    a.rejectBeforeDispatch = true;
    input.released = confirm;
    a.loop();
    assert(a.actions == previous && a.notifications == 1 && a.requested > 0);
    return 0;
  }
  a.build();
  if (kind == "reserve" || kind == "grow") assert(failedAllocations == 1);
  if (check == "gesture-state") {
    assert(!a.panelHoldJumped);
    assert(a.toolbarUi->contact.active == -1 && a.toolbarUi->contact.last_drag_x == -1 &&
           !a.toolbarUi->contact.contact_held);
  }
  const auto previous = a.actions;
  if (check == "button") {
    input.released = confirm;
    a.loop();
    assert(a.actions == previous);
  }
  if (check == "touch") {
    input.touchFlags = XP_UI_TOUCH_PRESSED | XP_UI_TOUCH_HELD;
    a.loop();
    input.touchFlags = XP_UI_TOUCH_RELEASED;
    a.loop();
    assert(a.actions == previous);
  }
  if (check == "notification") assert(a.notifications == 1);
  if (check == "retry") {
    a.loop();
    assert(a.requested > 0);
  }
  if (check == "busy") {
    RenderLock busy;
    auto inputTask = std::async(std::launch::async, [&] {
      input.released = confirm;
      a.loop();
    });
    assert(inputTask.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    inputTask.get();
    assert(a.actions == previous);
  }
  if (check == "back") {
    input.released = 1u << static_cast<unsigned>(MappedInputManager::Button::Back);
    a.loop();
    assert(a.cancelled == 1);
    {
      RenderLock lock;
      a.render(std::move(lock));
    }
    assert(a.pageDrawn == 1 && !a.rejected());
    return 0;
  }
  allocationFails = false;
  failure = 0;
  r.paintOk = true;
  a.build();
  if (check == "held-button") {
    input.held = confirm;
    a.loop();
    input.held = 0;
    input.released = confirm;
    a.loop();
    assert(a.actions == previous);
  }
  if (check == "held-touch") {
    input.touchFlags = XP_UI_TOUCH_HELD;
    a.loop();
    input.touchFlags = XP_UI_TOUCH_RELEASED;
    a.loop();
    assert(a.actions == previous);
  }
  input = {};
  a.loop();
  input.released = confirm;
  a.loop();
  assert(a.actions == previous + 1);
}
