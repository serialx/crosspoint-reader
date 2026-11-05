#include <xp_ui_reader.h>
#include <xp_ui_reader_chrome.h>

#include <cassert>
#include <chrono>
#include <cstring>
#include <future>
#include <mutex>
#include <type_traits>

static_assert(
    std::is_same_v<decltype(&xp_ui_reader_toolbar_nav_publish), void (*)(XpUiListState*, const XpUiListState*)>);
static_assert(sizeof(XpUiListState::navigation_revision) == 4);
#define taskENTER_CRITICAL(mutex) (mutex)->lock()
#define taskEXIT_CRITICAL(mutex) (mutex)->unlock()
struct ReaderToolbarUi {
  mutable std::mutex mutex;
  bool ready = false;
  XpUiReaderToolbarInput contact{};
  XpUiListState nav_{};
  void begin();
  void resetNavigation(int selected = 0, int top = 0);
  bool scrollNavigation(int delta, int count);
  int visibleRows() const;
};
// TOOLBAR_NAVIGATION_METHODS

static void oldPublish(XpUiListState& state, const XpUiListState& painted) {
  // Original ReaderToolbarUi copied these five SDK ListNav fields after painting.
  state.selected = painted.selected;
  state.top = painted.top;
  state.visible = painted.visible;
  state.drawn = painted.drawn;
  state.drawn_count = painted.drawn_count;
}

int main() {
  ReaderToolbarUi toolbar;
  for (const bool reset : {false, true}) {
    toolbar.begin();
    toolbar.resetNavigation(20, 20);
    toolbar.nav_.visible = 7;
    const auto snapshot = toolbar.nav_;
    auto painted = snapshot;
    painted.selected = 22;
    painted.top = 18;
    painted.visible = 9;
    painted.drawn = 8;
    painted.drawn_count = 100;
    std::mutex panel;
    const std::lock_guard<std::mutex> panelBusy(panel);
    auto input = std::async(std::launch::async, [&] {
      if (reset) {
        toolbar.resetNavigation(3, 3);
      } else {
        assert(toolbar.scrollNavigation(7, 100));
      }
    });
    // The real navigation methods must finish while painting remains busy.
    assert(input.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    input.get();
    const auto expected = toolbar.nav_;
    auto old = expected;
    oldPublish(old, painted);
    assert(old.top != expected.top && old.visible != expected.visible);
    {
      const std::lock_guard<std::mutex> model(toolbar.mutex);
      xp_ui_reader_toolbar_nav_publish(&toolbar.nav_, &painted);
    }
    assert(std::memcmp(&toolbar.nav_, &expected, sizeof(expected)) == 0);
    painted.navigation_revision = toolbar.nav_.navigation_revision;
    xp_ui_reader_toolbar_nav_publish(&toolbar.nav_, &painted);
    assert(toolbar.nav_.top == 18 && toolbar.nav_.selected == 22);
    assert(toolbar.nav_.visible == 9 && toolbar.nav_.drawn == 8 && toolbar.nav_.drawn_count == 100);
    assert(toolbar.visibleRows() == 8);
  }
  toolbar.nav_.navigation_revision = UINT32_MAX;
  const auto beforeWrap = toolbar.nav_;
  toolbar.resetNavigation();
  assert(toolbar.nav_.navigation_revision == 0);
  xp_ui_reader_toolbar_nav_publish(&toolbar.nav_, &beforeWrap);
  assert(toolbar.nav_.top == 0 && toolbar.nav_.visible == 1);
  const auto unchanged = toolbar.nav_;
  assert(!toolbar.scrollNavigation(-1, 100));
  assert(std::memcmp(&toolbar.nav_, &unchanged, sizeof(unchanged)) == 0);
  xp_ui_reader_toolbar_nav_publish(nullptr, &unchanged);
  xp_ui_reader_toolbar_nav_publish(&toolbar.nav_, nullptr);
  xp_ui_reader_toolbar_nav_publish(nullptr, nullptr);
  assert(std::memcmp(&toolbar.nav_, &unchanged, sizeof(unchanged)) == 0);
}
