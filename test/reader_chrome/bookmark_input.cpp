#include <FreeInkUI.h>
#include <xp_ui_reader_chrome.h>

#include <atomic>
#include <cassert>
#include <cstdio>
#include <string>
namespace fui = freeink::ui;
struct MappedInputManager {
  enum class Button {
    Back,
    Confirm,
    NavNext,
    NavPrevious,
    Left,
    Right,
    Up,
    Down,
    Power,
    PageBack,
    PageForward,
    ScreenLeft,
    ScreenRight,
    ScreenUp,
    ScreenDown
  };
  enum class SwipeDir { None, Up, Down, Left, Right };
  SwipeDir swipe = SwipeDir::None;
  uint32_t released = 0;
  bool tap = false, lift = false, longTouch = false;
  int x = 30, y = 130;
  bool hasTouch() const { return true; }
  bool wasReleased(Button b) const { return released & (1u << static_cast<int>(b)); }
  bool wasPressed(Button) const { return false; }
  bool isPressed(Button) const { return false; }
  unsigned getHeldTime() const { return 0; }
  bool wasScreenTapped(int& a, int& b) const {
    a = x;
    b = y;
    return tap;
  }
  bool wasScreenTouchReleased() const { return lift; }
  bool wasScreenLongPress(int& a, int& b) const {
    a = x;
    b = y;
    return longTouch;
  }
  bool isScreenTouchHeld(int&, int&) const { return false; }
  bool wasScreenTouchDown(int&, int&) const { return false; }
  SwipeDir wasSwipe(int* a = nullptr, int* b = nullptr) const {
    if (a) *a = x;
    if (b) *b = y;
    return swipe;
  }
};
unsigned millis() { return 1000; }
// SNAPSHOT
struct RenderLock {
  template <class T>
  explicit RenderLock(T&) {}
};
struct UiAppHost {
  static void routeTouch(const MappedInputManager&, bool) {}
};
struct Published {
  bool hitPublished(int x, int y, int action, fui::Interaction& hit) {
    if (x < 0 || x >= 200 || y < 0 || y >= 240) return false;
    const int row = 4 + y / 60;
    if (action == 2 && (x < 150 || row != 5)) return false;
    hit.value = row;
    return true;
  }
  void route(fui::InputSnapshot) {}
  void clearTapFlash() {}
};
struct UiListActivity {
  static constexpr int ACTION_ROW = 1, ACTION_SWIPE_DELETE = 2;
  MappedInputManager mappedInput;
  std::atomic<int> revealedIndex{5};
  Published app;
  fui::ListNav nav;
  bool wantsTouchLongPress = true;
  std::string name = "bookmarks";
  int deletion = -1, activation = -1, actions = -1;
  bool invalidated = false;
  UiListActivity() {
    nav.selected = 5;
    nav.followOnBuild = false;
    nav.visibleRows = 5;
    nav.drawnCount = 20;
    nav.onListRendered(4, 4, true);
  }
  fui::ListNav& activeNav() { return nav; }
  bool routingReady() { return true; }
  bool canSwipeDelete(int row) { return row >= 0 && row < 20; }
  int listCount() { return 20; }
  void requestUpdate() { invalidated = true; }
  static void rowActionTrampoline(const fui::ActionEvent&, void*);
  void onRowAction(const fui::ActionEvent&);
  void activateIndex(int row) { activation = row; }
  void onRowLongPress(int row) { actions = row; }
  void closeSwipeDelete();
  bool handleSwipeDeleteInput();
  void loop();
  bool handleCustomInput() { return false; }
  bool handleButtons() { return false; }
  void navigateButtons() {}
  bool routeListTouch() {
    fui::Interaction hit;
    if (mappedInput.longTouch && app.hitPublished(mappedInput.x, mappedInput.y, 1, hit)) {
      fui::ActionEvent event{};
      event.value = hit.value;
      event.longPress = true;
      rowActionTrampoline(event, this);
      return true;
    }
    if (!mappedInput.tap) return false;
    if (app.hitPublished(mappedInput.x, mappedInput.y, 2, hit) && hit.value == revealedIndex) {
      deletion = hit.value;
      closeSwipeDelete();
      return true;
    }
    return false;
  }
};
#define LOG_DBG(...) ((void)0)
// BASELINE
struct ReaderChromeListActivity {
  XpUiReaderListState state{};
  XpUiReaderHitLayout publishedHits{};
  struct {
    bool ready = true;
  } publication;
  int kind = XP_UI_READER_BOOKMARKS;
  bool touchActivated = false, nativeLongPress = false, nativeLongTouch = false;
  int nativeTouchX = 0, nativeTouchY = 0;
  int listCount() const { return 20; }
  bool hasModel() const { return true; }
  XpUiEffect updateRust(const XpUiInput& source);
};
// BRIDGE
XpUiInput adapter(const MappedInputManager& mappedInput) {
  // ADAPTER
  return input;
}
int main() {
  using Swipe = MappedInputManager::SwipeDir;
  struct Case {
    const char* name;
    Swipe swipe;
    bool tap;
    uint32_t released;
    int x, y;
    int reveal, scroll, deletion;
    int initialReveal = 5;
    bool longTouch = false;
    int actions = -1;
  };
  const Case cases[] = {
      {"up release", Swipe::Up, false, 0, 30, 130, -1, 4, -1},
      {"down release", Swipe::Down, false, 0, 30, 130, -1, -4, -1},
      {"left replaces", Swipe::Left, false, 0, 30, 130, 6, 0, -1},
      {"left misses", Swipe::Left, false, 0, -1, -1, 5, 0, -1},
      {"right closes", Swipe::Right, false, 0, 30, 130, -1, 0, -1},
      {"outside tap", Swipe::None, true, 0, 250, 250, -1, 0, -1},
      {"row tap", Swipe::None, true, 0, 30, 130, -1, 0, -1},
      {"delete tap", Swipe::None, true, 0, 170, 90, -1, 0, 5},
      {"stale delete tap", Swipe::None, true, 0, 170, 90, -1, 0, -1, 6},
      {"back and delete tap", Swipe::None, true, XP_UI_BACK, 170, 90, -1, 0, -1},
      {"back closes", Swipe::None, false, XP_UI_BACK, 30, 130, -1, 0, -1},
      {"confirm closes", Swipe::None, false, XP_UI_CONFIRM, 30, 130, -1, 0, -1},
      {"cancelled release", Swipe::None, false, 0, 30, 130, 5, 0, -1},
      {"long touch closes", Swipe::None, false, 0, 30, 130, -1, 0, -1, 5, true, -1},
      {"long touch actions", Swipe::None, false, 0, 30, 130, -1, 0, -1, -1, true, 6},
      {"long touch misses", Swipe::None, false, 0, -1, -1, -1, 0, -1, -1, true, -1},
  };
  for (const auto& c : cases) {
    UiListActivity old;
    old.revealedIndex = c.initialReveal;
    old.mappedInput.longTouch = c.longTouch;
    old.mappedInput.swipe = c.swipe;
    old.mappedInput.tap = c.tap;
    old.mappedInput.lift = true;
    old.mappedInput.released = c.released;
    old.mappedInput.x = c.x;
    old.mappedInput.y = c.y;
    old.loop();
    // The original SDK viewport consumes the queued scroll during layout.
    fui::ListProps props;
    props.count = 20;
    old.nav.syncToProps({0, 0, 200, 240}, 60, 0, 20, props);
    assert(old.revealedIndex == c.reveal && old.deletion == c.deletion);
    assert(old.nav.top == 4 + c.scroll && old.nav.selected == (c.actions >= 0 ? c.actions : 5));
    ReaderChromeListActivity now;
    xp_ui_reader_list_init(&now.state, 5);
    now.state.list.top = 4;
    now.state.list.follow = 0;
    now.nativeLongTouch = c.longTouch;
    now.state.revealed = c.initialReveal;
    now.state.list.visible = 5;
    now.state.list.drawn = 4;
    now.state.list.drawn_count = 20;
    now.nativeTouchX = c.x;
    now.nativeTouchY = c.y;
    now.publishedHits.first_row = {0, 0, 200, 60};
    now.publishedHits.first_index = 4;
    now.publishedHits.row_count = 4;
    now.publishedHits.row_action = 1;
    now.publishedHits.uniform = 1;
    now.publishedHits.generation = 1;
    for (int i = 0; i < 4; ++i) now.publishedHits.heights[i] = 60;
    now.publishedHits.control_count = 1;
    now.publishedHits.controls[0] = {{150, 60, 50, 60}, 5, 2, {0, 0, 0}};
    const auto input = adapter(old.mappedInput);
    assert(input.touch_flags & XP_UI_TOUCH_RELEASED);
    if (!c.tap) assert(input.touch_x == -1 && input.touch_y == -1 && input.action == 0);
    const auto effect = now.updateRust(input);
    std::printf("%s: reveal %d scroll %d effect %d\n", c.name, now.state.revealed, now.state.list.pending_scroll,
                effect.kind);
    std::fflush(stdout);
    assert(now.state.revealed == c.reveal);
    assert(now.state.list.pending_scroll == c.scroll && now.state.list.selected == (c.actions >= 0 ? c.actions : 5));
    assert((effect.kind == XP_UI_READER_DELETE ? effect.value : -1) == c.deletion);
    assert(old.actions == c.actions);
    assert((effect.kind == XP_UI_READER_ACTIONS ? effect.value : -1) == c.actions);
  }
}
