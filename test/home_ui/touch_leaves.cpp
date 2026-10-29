#include <FreeInkApp.h>
#include <atomic>
#include "MappedInputManager.h"
namespace fui = freeink::ui;
static int scrollDelta;
#define LOG_DBG(a,b,c,d,e) scrollDelta=d
struct RenderLock { template<class T> explicit RenderLock(T&) {} };
struct UiAppHost {
  static bool routeTouch(const MappedInputManager&, bool) { return false; }
};
class UiListActivity {
 public:
  static constexpr int ACTION_ROW = 1, ACTION_SWIPE_DELETE = 2;
  bool wantsTouchLongPress = true, ready = true, library = false, picker = false, collapsed = false;
  int tab = 0, count = 5, effect = 0, value = -1, updates = 0, haptic = 0;
  std::string name = "test";
  std::atomic<int> revealedIndex{-1};
  fui::ListNav nav;
  MappedInputManager mappedInput;
  struct App {
    int action = 0, value = 0;
    bool hitPublished(int x, int y, int wanted, fui::Interaction& out) {
      if (x < 0 || y < 0 || wanted != action) return false;
      out.action = action; out.value = value; return true;
    }
    void clearTapFlash() {}
    void route(fui::InputSnapshot) {}
  } app;
  bool routingReady() const { return ready; }
  int listCount() const { return count; }
  fui::ListNav& activeNav() { return nav; }
  bool canSwipeDelete(int i) const { return !picker && !collapsed && i >= 0 && i < count; }
  void requestUpdate() { ++updates; }
  void closeSwipeDelete();
  bool handleSwipeDeleteInput();
  static void rowActionTrampoline(const fui::ActionEvent&, void*);
  static void swipeDeleteTrampoline(const fui::ActionEvent&, void*);
  void loop();
  bool handleCustomInput() { return false; }
  bool handleButtons() { return false; }
  void navigateButtons() {}
  void onRowAction(const fui::ActionEvent& event) {
    nav.selected = event.value + (library ? 1 : 0);
    value = event.value;
    effect = event.longPress ? (library ? (tab == 0 ? 6 : collapsed ? 2 : 8) : picker ? 2 : 7) : 2;
  }
  void swipeDelete(int i) { nav.selected = i + (library ? 1 : 0); effect = 11; value = i; }
  bool routeListTouch() {
    if (!ready || mappedInput.touch == 0 || mappedInput.x < 0 || mappedInput.y < 0) return false;
    fui::ActionEvent event{};
    event.action = app.action; event.value = app.value; event.longPress = mappedInput.touch == 3;
    if (event.action == 1) rowActionTrampoline(event, this);
    else if (event.action == 2 && !event.longPress) swipeDeleteTrampoline(event, this);
    else return false;
    ++haptic; return true;
  }
};
