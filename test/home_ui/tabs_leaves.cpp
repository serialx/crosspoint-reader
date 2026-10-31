#include <algorithm>
using UiScreen = ui::Screen<24>;
namespace fui = freeink::ui;
struct GfxRenderer {};
struct Metrics {
  bool tabPillFullSlot = false;
  int tabSpacing = 8, tabBarHeight = 48, headerUnderlineSize = 3, contentSidePadding = 20, verticalSpacing = 16;
};
struct UITheme {
  static UITheme& getInstance() {
    static UITheme t;
    return t;
  }
  Metrics metrics;
  const Metrics& getMetrics() { return metrics; }
};
struct Input {
  bool touch = false;
  bool hasTouch() { return touch; }
};
constexpr int16_t TOUCH_TAB_BAR_HEIGHT = 50;
class UiTabListActivity {
 public:
  static constexpr int ACTION_TAB = 3;
  int selected = 0, active = 0;
  bool descending = false;
  int tabPillMaxPad = 16;
  Input mappedInput;
  int tabCount() { return 3; }
  int ringPos() { return selected; }
  int activeTab() { return active; }
  const char* tabLabel(int i) { return i == 0 ? "Recent" : i == 1 ? "Title" : "Author"; }
  fui::TabIndicator tabIndicator(int i) {
    return i != active ? fui::TabIndicator::None : descending ? fui::TabIndicator::Down : fui::TabIndicator::Up;
  }
  void buildTabBar(UiScreen&);
};
