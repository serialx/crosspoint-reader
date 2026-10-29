#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>
static unsigned eventIndex = 0, lockDepth = 0;
static void event(const char* name, int a = 0, int b = 0) {
  std::printf("%u %s %d %d lock=%u\n", eventIndex++, name, a, b, lockDepth);
}
struct RenderLock {
  RenderLock() {
    ++lockDepth;
    event("lock");
  }
  ~RenderLock() {
    event("unlock");
    assert(lockDepth);
    --lockDepth;
  }
};
struct MappedInputManager {
  enum class Button { Back, Left, Right, Confirm, Up, Down };
  enum class SwipeDir { None, Up, Down, Left, Right };
  unsigned released = 0, pressed = 0, held = 0;
  SwipeDir swipe = SwipeDir::None;
  bool wasReleased(Button b) {
    bool v = released & (1u << int(b));
    event("released", int(b), v);
    return v;
  }
  bool isPressed(Button b) {
    bool v = pressed & (1u << int(b));
    event("pressed", int(b), v);
    return v;
  }
  unsigned getHeldTime() {
    event("held", held);
    return held;
  }
  SwipeDir wasSwipe() {
    event("swipe", int(swipe));
    return swipe;
  }
};
struct ReaderToolbarUi {
  enum class Event { None = 0, Dismiss = 1, Tool = 2, PrevChapter = 3, NextChapter = 4, Scrub = 5, Row = 6 };
  struct Routed {
    Event event = Event::None;
    int value = 0, permille = -1;
    bool routed = false;
    int x = 0, y = 0;
  } routed;
  int rows = 3;
  bool scrollMoved = true;
  Routed route(MappedInputManager&) {
    event("route", int(routed.event), routed.routed);
    return routed;
  }
  int visibleRows() {
    event("rows", rows);
    return rows;
  }
  ReaderToolbarUi& nav() { return *this; }
  bool scrollBy(int delta, int count) { return scrollNavigation(delta, count); }
  bool scrollNavigation(int delta, int count) {
    event("scroll", delta, count);
    return scrollMoved;
  }
  void begin() { event("toolbar-begin"); }
};
struct Renderer {
  int width = 600, height = 800;
  bool store = true;
  int getScreenWidth() {
    event("width", width);
    return width;
  }
  int getScreenHeight() {
    event("height", height);
    return height;
  }
  void restoreBwBuffer(bool resync) { event("restore", resync); }
  bool storeBwBuffer() {
    event("store", store);
    return store;
  }
};
struct Section {
  ~Section() { event("section-drop"); }
};
struct Epub {
  int count = 7, tocCount = 12, tocSpine = 3;
  int getSpineItemsCount() {
    event("spines", count);
    return count;
  }
  int getTocItemsCount() {
    event("toc-count", tocCount);
    return tocCount;
  }
  struct TocItem {
    int spineIndex;
    std::string anchor;
    bool owns = true;
    TocItem(int spine, std::string text) : spineIndex(spine), anchor(std::move(text)) { event("toc-create"); }
    TocItem(TocItem&& other) : spineIndex(other.spineIndex), anchor(std::move(other.anchor)) { other.owns = false; }
    ~TocItem() {
      if (owns) event("toc-drop");
    }
  };
  TocItem getTocItem(int i) {
    event("toc-item", i, tocSpine);
    return {tocSpine, "anchor"};
  }
};
struct Popup {
  bool active = false, after = false;
  bool isActive() {
    event("popup-active", active);
    return active;
  }
  void dismiss() {
    event("dismiss");
    active = false;
  }
  template <class F>
  void handleInput(MappedInputManager&, F fn) {
    event("popup-input");
    active = after;
    fn();
  }
};
struct ActivityResult {};
struct TextSettingsActivity {
  enum class Tab { Family };
  TextSettingsActivity(Renderer&, MappedInputManager&, int*, Tab) { event("font-picker-create"); }
  ~TextSettingsActivity() { event("font-picker-drop"); }
};
struct {
  int value = 0;
  int& registry() {
    event("registry");
    return value;
  }
} sdFontSystem;
struct {
  int focusReadingEnabled = 0;
} SETTINGS;
constexpr int kTextRowCount = 5;
struct ButtonNavigator {
  static int nextIndex(int, int);
  static int previousIndex(int, int);
};
// NAV_METHODS
struct EpubReaderActivity {
  enum class Overlay { None, Toolbar, Contents, Text, More };
  Overlay overlay = Overlay::None;
  int currentSpineIndex = 3, nextPageNumber = 8, focusedTool = 0, panelIndex = 0;
  bool panelCursorShown = false, panelHoldJumped = false, overlayPageStored = false;
  static constexpr unsigned PANEL_HOLD_MS = 1500;
  static constexpr int PANEL_HOLD_STEP = 10;
  std::string pendingAnchor;
  std::unique_ptr<ReaderToolbarUi> toolbarUi = std::make_unique<ReaderToolbarUi>();
  std::unique_ptr<Epub> epub = std::make_unique<Epub>();
  std::unique_ptr<Section> section = std::make_unique<Section>();
  Popup overlayPopup;
  Renderer renderer;
  MappedInputManager mappedInput;
  std::vector<int> moreItems = std::vector<int>(6);
  std::function<void(const ActivityResult&)> result;
  void paintOverlayPopup() { event("paint-popup"); }
  void settleOverlayRefresh() { event("settle"); }
  void renderOverlay() { event("render", int(overlay), panelIndex); }
  void pushOverlayRefresh() { event("refresh"); }
  void requestUpdate() { event("update"); }
  void clearDeferredReposition() { event("clear-position"); }
  void closeOverlayToPage() {
    event("close");
    overlay = Overlay::None;
  }
  void openOverlay(Overlay target) {
    event("open", int(target));
    overlay = target;
  }
  void discardOverlayPage() {
    event("discard");
    overlayPageStored = false;
  }
  void applyReaderTextSettings() { event("text-settings"); }
  void applyTextSettingLive() { event("apply-live", SETTINGS.focusReadingEnabled); }
  void showTextRowPopup(int row) { event("text-popup", row); }
  void activateMoreRow(int row) { event("more", row); }
  void startActivityForResult(std::unique_ptr<TextSettingsActivity> child,
                              std::function<void(const ActivityResult&)> fn) {
    event("start-child");
    result = std::move(fn);
  }
  void handleOverlayInput();
  void dump() {
    std::printf(
        "state overlay=%d spine=%d next=%d tool=%d row=%d cursor=%d jumped=%d stored=%d section=%d anchor=%s "
        "focus=%d\n",
        int(overlay), currentSpineIndex, nextPageNumber, focusedTool, panelIndex, panelCursorShown, panelHoldJumped,
        overlayPageStored, bool(section), pendingAnchor.c_str(), SETTINGS.focusReadingEnabled);
    assert(lockDepth == 0);
  }
};
// OVERLAY_METHODS
int main() {
  unsigned id = 0;
  for (int mode = 1; mode <= 4; ++mode)
    for (int scenario = 0; scenario < 36; ++scenario)
      for (int variant = 0; variant < 12; ++variant) {
        eventIndex = 0;
        SETTINGS.focusReadingEnabled = variant % 2;
        EpubReaderActivity r;
        r.overlay = static_cast<EpubReaderActivity::Overlay>(mode);
        r.panelIndex = variant % 6 - 1;
        r.focusedTool = variant % 3;
        r.panelCursorShown = variant & 1;
        r.panelHoldJumped = variant & 2;
        r.overlayPageStored = variant & 4;
        r.epub->count = variant % 3 == 0 ? 1 : 7;
        r.epub->tocCount = variant % 4 == 0 ? 0 : 12;
        r.epub->tocSpine = variant % 3 == 0 ? -1 : 3;
        r.currentSpineIndex = variant % 3;
        r.renderer.width = variant & 1 ? 800 : 600;
        r.renderer.height = variant & 1 ? 600 : 800;
        r.renderer.store = !(variant & 2);
        r.toolbarUi->rows = variant % 3 == 0 ? 0 : 3;
        r.toolbarUi->scrollMoved = variant & 1;
        auto& t = r.toolbarUi->routed;
        if (scenario < 7) {
          t.event = static_cast<ReaderToolbarUi::Event>(scenario);
          t.routed = true;
          t.value = variant % 3;
          t.permille = variant * 100;
          t.x = variant & 1 ? r.renderer.width - 44 : 10;
          t.y = variant & 2 ? r.renderer.height - 10 : 10;
        } else if (scenario < 13)
          r.mappedInput.released = 1u << (scenario - 7);
        else if (scenario < 19) {
          r.mappedInput.pressed = 1u << (scenario - 13);
          r.mappedInput.held = variant % 3 == 0 ? 1499 : 1500;
        } else if (scenario < 24) {
          r.mappedInput.swipe = static_cast<MappedInputManager::SwipeDir>(scenario - 19);
          t.routed = variant & 1;
        } else if (scenario < 27) {
          r.overlayPopup.active = true;
          r.overlayPopup.after = scenario == 24;
          r.overlayPageStored = scenario == 25;
        } else if (scenario == 27)
          r.toolbarUi.reset();
        else if (scenario == 28) {
          t.routed = true;
        } else if (scenario == 29) {
          r.mappedInput.released =
              (1u << int(MappedInputManager::Button::Up)) | (1u << int(MappedInputManager::Button::Down));
        } else if (scenario == 30) {
          r.mappedInput.pressed = 48;
          r.mappedInput.held = 1500;
        } else if (scenario == 31) {
          t.event = ReaderToolbarUi::Event::Row;
          t.value = variant - 1;
          t.x = 10;
        } else if (scenario == 32) {
          r.mappedInput.released = 8;
          r.panelIndex = 4;
        } else if (scenario == 33) {
          r.mappedInput.released = 8;
          r.panelIndex = 0;
        } else if (scenario == 34) {
          r.mappedInput.released = 8;
          r.panelIndex = 2;
        } else {
          r.mappedInput.released = 1;
          r.overlayPageStored = true;
        }
        std::printf("case=%u mode=%d scenario=%d variant=%d\n", id++, mode, scenario, variant);
        r.handleOverlayInput();
        r.dump();
        if (r.result) {
          event("child-result");
          r.result({});
          r.dump();
        }
        if (scenario >= 13 && scenario < 19) {
          r.mappedInput.released = r.mappedInput.pressed;
          r.mappedInput.pressed = 0;
          r.handleOverlayInput();
          r.dump();
        }
      }
}
