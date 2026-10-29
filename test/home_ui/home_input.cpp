#include "ButtonNavigator.h"
struct Rect { int x, y, width, height; };
struct Metrics {
  int homeRecentBooksCount = 3, contentSidePadding = 12, homeTopPadding = 80, homeCoverTileHeight = 240;
  int homeMenuTopOffset = 12, menuSpacing = 6;
  bool homeContinueReadingInMenu = false;
};
struct UITheme {
  Metrics metrics;
  static UITheme& getInstance() { static UITheme t; return t; }
  const Metrics& getMetrics() { return metrics; }
  Rect getContentArea(int) { return {10,0,508,792}; }
  int getMenuRowHeight(int) { return 36; }
};
#define GUI UITheme::getInstance()
static int haptics;
namespace haptic_feedback { inline void touchAction() { ++haptics; } }
enum class HomeMenuItem { NONE, FILE_BROWSER, LIBRARY, OPDS_BROWSER, FILE_TRANSFER, SETTINGS_MENU };
struct RecentBook { std::string path; };
struct CoverGridHomeUi { int selectedAction(const MappedInputManager&) { return -1; } };
class HomeActivity {
 public:
  MappedInputManager mappedInput;
  ButtonNavigator buttonNavigator;
  std::unique_ptr<CoverGridHomeUi> coverGridUi;
  std::vector<RecentBook> recentBooks;
  bool hasPlugins = false, hasOpdsServers = false, hasContinueReading = false;
  int renderer = 0, selectorIndex = 0, effect = -1, updates = 0;
  bool hasLibrarySlot() const { return hasPlugins || hasOpdsServers; }
  int getMenuItemCount() const;
  static int menuItemToIndex(HomeMenuItem, bool);
  static HomeMenuItem indexToMenuItem(int, bool);
  void loop();
  void requestUpdate() { ++updates; }
  void onRecentBookSelected(const std::string& p) { effect = std::stoi(p); }
  void onFileBrowserOpen() { effect = 100; }
  void onLibraryOpen() { effect = 101; }
  void onOpdsBrowserOpen() { effect = 102; }
  void onPluginsOpen() { effect = 103; }
  void onFileTransferOpen() { effect = 104; }
  void onSettingsOpen() { effect = 105; }
};
