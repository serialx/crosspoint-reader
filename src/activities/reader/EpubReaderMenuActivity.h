#pragma once
#include <Epub.h>
#include <I18n.h>

#include <string>
#include <vector>

#include "ReaderChromeListActivity.h"
#include "components/OptionPopup.h"

class EpubReaderMenuActivity final : public ReaderChromeListActivity {
 public:
  // Menu actions available from the reader menu.
  enum class MenuAction {
    SELECT_CHAPTER,
    FOOTNOTES,
    TEXT_SETTINGS,
    NIGHT_MODE,
    FRONTLIGHT,
    BLUETOOTH,
    GO_TO_PERCENT,
    AUTO_PAGE_TURN,
    ROTATE_SCREEN,
    BOOKMARKS,
    SAVE_CLIPPING,
    VIEW_CLIPPINGS,
    TOGGLE_BOOKMARK,
    SCREENSHOT,
    DISPLAY_QR,
    GO_HOME,
    SYNC,
    DELETE_CACHE,
    DICTIONARY
  };

  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static void toggleBluetooth();
  static void buildMenuItems(std::vector<MenuItem>& items, bool hasFootnotes, bool hasBookmarks, bool hasClippings);

  explicit EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                  const int currentPage, const int totalPages, const int bookProgressPercent,
                                  const uint8_t currentOrientation, const bool hasFootnotes, bool hasBookmarks,
                                  bool hasClippings);

  void render(RenderLock&&) override;
  bool handleHomeGesture() override;
  bool acceptsRemoteInput() const override { return routingReady(); }

 private:
  // Row storage: menuItems is at most MAX_MENU_ITEMS, so a
  // fixed-capacity array avoids any heap allocation for the row list. Labels
  // are set once; prepareRows refreshes values from the existing services.
  static constexpr size_t MAX_MENU_ITEMS = 19;
  ReaderChromeRow menuRowItems[MAX_MENU_ITEMS]{};
  void buildMenuRowItems();

  int listCount() const override { return static_cast<int>(menuItems.size()); }
  void prepareRows(int) override;
  const ReaderChromeRow* rowAt(int index) const override;
  const char* headerTitle() const override { return title.c_str(); }
  const char* progressText() const override { return progressLine.c_str(); }
  void onBackButton() override { closeCancelled(); }
  void activateIndex(int index) override;
  // Popup input runs before any button or touch handling.
  bool handleCustomInput() override;
  void closeCancelled();

  // Fixed menu layout
  std::vector<MenuItem> menuItems;

  OptionPopup optionPopup;
  std::string title = "Reader Menu";
  XpUiReaderMenuState menuState{};
  void applyMenuEffect(const XpUiReaderMenuResult& effect);
  void chooseOption(uint8_t option, int index);
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  const std::vector<const char*> pageTurnLabels = {I18N.get(StrId::STR_STATE_OFF), "1", "3", "6", "12"};
  std::string progressLine;
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
};
