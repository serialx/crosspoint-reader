#include <FreeInkApp.h>

#include "ButtonNavigator.h"
namespace fui = freeink::ui;
namespace library {
enum class SortOrder { RecentAsc, RecentDesc, TitleAsc, TitleDesc, AuthorAsc, AuthorDesc };
}
constexpr int LONG_PRESS_MS = 1000, TITLE_TAB = 1, AUTHOR_TAB = 2, TAB_SLOTS = 3;
struct OptionPopup {
  template <class T>
  bool handleInput(const MappedInputManager&, T) {
    return false;
  }
};
class LibraryListActivity {
 public:
  MappedInputManager mappedInput;
  ButtonNavigator buttonNavigator;
  fui::ListNav navs[3];
  OptionPopup optionPopup;
  int activeTabIndex = 0, count = 0, updates = 0;
  uint8_t descendingTabs = 1;
  library::SortOrder sortOrder = library::SortOrder::RecentDesc;
  bool degraded = false, groupsCollapsed = false, lockNextConfirmRelease = false, lockNextBackRelease = false,
       navigationStartedOnTabs = false;
  std::string query, effect;
  fui::ListNav& activeNav() { return navs[activeTabIndex]; }
  int ringPos() const { return navs[activeTabIndex].selected; }
  bool tabsFocused() const { return ringPos() == 0; }
  int activeTab() const { return activeTabIndex; }
  int listCount() const { return count; }
  int bookRowCount() const { return count; }
  void requestUpdate() { ++updates; }
  void moveRingTo(int i) {
    activeNav().requestSelection(i);
    requestUpdate();
  }
  void onGoHome() { effect = "home"; }
  void openSearch() { effect = "search"; }
  void showRecentBookOptions(int i) { effect = "options:" + std::to_string(i); }
  void promptDeleteBook(int i) { effect = "delete:" + std::to_string(i); }
  bool collapseGroups(int i) {
    effect = "collapse:" + std::to_string(i);
    return true;
  }
  void activateIndex(int i) { effect = "activate:" + std::to_string(i); }
  void restoreExpandedList() { effect = "expand"; }
  void refreshOverlap() {}
  void applyFilter() { groupsCollapsed = false; }
  int selectedEntry() const;
  bool groupable() const;
  bool deleteEligible() const;
  void selectTab(int, bool);
  void toggleSortDirection();
  void stepTab(int);
  void handleBackAction();
  bool handleCustomInput();
  bool handleButtons();
  void navigateButtons();
};
