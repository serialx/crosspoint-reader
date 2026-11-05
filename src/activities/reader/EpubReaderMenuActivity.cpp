#include "EpubReaderMenuActivity.h"

#include <BlePageTurner.h>
#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "util/SlideSwitch.h"

static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::NIGHT_MODE) == XP_UI_READER_MENU_NIGHT);
static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::FRONTLIGHT) == XP_UI_READER_MENU_FRONTLIGHT);
static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::BLUETOOTH) == XP_UI_READER_MENU_BLUETOOTH);
static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::AUTO_PAGE_TURN) == XP_UI_READER_MENU_AUTO_TURN);
static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::ROTATE_SCREEN) == XP_UI_READER_MENU_ORIENTATION);
static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::DICTIONARY) == XP_UI_READER_MENU_LAST_ACTION);
static_assert(XP_UI_READER_MENU_NONE == 0 && XP_UI_READER_MENU_SHOW_ORIENTATION == 1 &&
              XP_UI_READER_MENU_SHOW_AUTO_TURN == 2 && XP_UI_READER_MENU_TOGGLE_NIGHT == 3 &&
              XP_UI_READER_MENU_TOGGLE_BLUETOOTH == 4 && XP_UI_READER_MENU_TOGGLE_FRONTLIGHT == 5 &&
              XP_UI_READER_MENU_FINISH == 6 && XP_UI_READER_MENU_APPLY_ORIENTATION == 7 &&
              XP_UI_READER_MENU_REDRAW == 8);

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const int currentPage, const int totalPages,
                                               const int bookProgressPercent, const uint8_t currentOrientation,
                                               const bool hasFootnotes, const bool hasBookmarks,
                                               const bool hasClippings)
    : ReaderChromeListActivity("EpubReaderMenu", renderer, mappedInput, XP_UI_READER_MENU, MAX_MENU_ITEMS),
      title(title),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {
  xp_ui_reader_menu_init(&menuState, currentOrientation);
  buildMenuItems(menuItems, hasFootnotes, hasBookmarks, hasClippings);
  buildMenuRowItems();
  if (totalPages > 0) {
    progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                   std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
}

// Labels borrow the immutable translation table for this visit.
void EpubReaderMenuActivity::buildMenuRowItems() {
  for (size_t i = 0; i < menuItems.size() && i < MAX_MENU_ITEMS; i++) {
    ReaderChromeRow item;
    item.label = I18N.get(menuItems[i].labelId);

    menuRowItems[i] = item;
  }
}

void EpubReaderMenuActivity::buildMenuItems(std::vector<MenuItem>& items, const bool hasFootnotes,
                                            const bool hasBookmarks, const bool hasClippings) {
  items.clear();
  items.reserve(MAX_MENU_ITEMS);
  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  if (hasFootnotes) {
    items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  }
  if (hasBookmarks) {
    items.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
  }
  items.push_back({MenuAction::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK});
  items.push_back({MenuAction::SAVE_CLIPPING, StrId::STR_SAVE_CLIPPING});
  if (hasClippings) {
    items.push_back({MenuAction::VIEW_CLIPPINGS, StrId::STR_VIEW_CLIPPINGS});
  }
  items.push_back({MenuAction::NIGHT_MODE, StrId::STR_NIGHT_MODE});
  if (Frontlight.present() && !slideSwitchOwnsLight()) {
    items.push_back({MenuAction::FRONTLIGHT, StrId::STR_FRONTLIGHT});
  }
  if (bleturner::status().compiledIn) {
    items.push_back({MenuAction::BLUETOOTH, StrId::STR_BT_PAGE_TURNER});
  }
  if (SETTINGS.dictionaryName[0] != '\0') {
    items.push_back({MenuAction::DICTIONARY, StrId::STR_LOOKUP});
  }
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});
  items.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN});
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
  items.push_back({MenuAction::TEXT_SETTINGS, StrId::STR_TEXT_SETTINGS});
}

void EpubReaderMenuActivity::toggleBluetooth() {
  SETTINGS.ble.enabled = SETTINGS.ble.enabled ? 0 : 1;
  if (!SETTINGS.ble.enabled) bleturner::switchOff();
  SETTINGS.saveToFile();
  // Enabling waits for the main-loop lifecycle's reader heap gates, then says in the status bar
  // whether a remote linked.
  if (SETTINGS.ble.enabled) bleturner::requestConnect();
}

void EpubReaderMenuActivity::closeCancelled() {
  XpUiReaderMenuResult effect{};
  {
    ModelLock lock(*this);
    effect = xp_ui_reader_menu_cancel(&menuState);
  }
  applyMenuEffect(effect);
}

bool EpubReaderMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void EpubReaderMenuActivity::activateIndex(const int index) {
  if (optionPopup.isActive() || index < 0 || index >= listCount()) return;
  selectIndex(index);
  XpUiReaderMenuResult effect{};
  {
    ModelLock lock(*this);
    effect = xp_ui_reader_menu_activate(&menuState, static_cast<int>(menuItems[index].action));
  }
  applyMenuEffect(effect);
}

void EpubReaderMenuActivity::chooseOption(uint8_t option, int index) {
  XpUiReaderMenuResult effect{};
  {
    ModelLock lock(*this);
    effect = xp_ui_reader_menu_choose(&menuState, option, index);
  }
  applyMenuEffect(effect);
}

void EpubReaderMenuActivity::applyMenuEffect(const XpUiReaderMenuResult& effect) {
  switch (static_cast<XpUiReaderMenuEffect>(effect.effect)) {
    case XP_UI_READER_MENU_NONE:
      break;
    case XP_UI_READER_MENU_SHOW_ORIENTATION:
      optionPopup.show(StrId::STR_ORIENTATION, orientationLabels.data(), static_cast<int>(orientationLabels.size()),
                       effect.orientation,
                       [this](int index) { chooseOption(XP_UI_READER_MENU_CHOOSE_ORIENTATION, index); });
      requestUpdate();
      break;
    case XP_UI_READER_MENU_SHOW_AUTO_TURN:
      optionPopup.show(I18N.get(StrId::STR_AUTO_TURN_PAGES_PER_MIN), pageTurnLabels.data(),
                       static_cast<int>(pageTurnLabels.size()), effect.page_turn,
                       [this](int index) { chooseOption(XP_UI_READER_MENU_CHOOSE_AUTO_TURN, index); });
      requestUpdate();
      break;
    case XP_UI_READER_MENU_TOGGLE_NIGHT:
      SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    case XP_UI_READER_MENU_TOGGLE_BLUETOOTH:
      toggleBluetooth();
      requestUpdate();
      break;
    case XP_UI_READER_MENU_TOGGLE_FRONTLIGHT: {
      const bool lightOn = !Frontlight.isOn();
      Frontlight.setOn(lightOn);
      SETTINGS.frontlightOn = lightOn ? 1 : 0;
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    }
    case XP_UI_READER_MENU_FINISH: {
      ActivityResult result;
      result.isCancelled = effect.cancelled;
      result.data = MenuResult{static_cast<int>(effect.action), effect.orientation, effect.page_turn};
      setResult(std::move(result));
      finish();
      break;
    }
    case XP_UI_READER_MENU_APPLY_ORIENTATION: {
      RenderLock lock(*this);
      ReaderUtils::applyOrientation(renderer, effect.orientation);
      requestUpdate(true);
      break;
    }
    case XP_UI_READER_MENU_REDRAW:
      requestUpdate();
      break;
  }
}

bool EpubReaderMenuActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderList();
}

void EpubReaderMenuActivity::prepareRows(int) {
  XpUiReaderMenuState snapshot{};
  {
    ModelLock lock(*this);
    snapshot = menuState;
  }
  for (size_t i = 0; i < menuItems.size(); ++i) {
    auto& row = menuRowItems[i];
    switch (menuItems[i].action) {
      case MenuAction::ROTATE_SCREEN:
        row.value = I18N.get(orientationLabels[snapshot.orientation]);
        break;
      case MenuAction::AUTO_PAGE_TURN:
        row.value = pageTurnLabels[snapshot.page_turn];
        break;
      case MenuAction::NIGHT_MODE:
        row.toggle = true;
        row.checked = SETTINGS.screenInverted;
        break;
      case MenuAction::BLUETOOTH:
        row.toggle = true;
        row.checked = SETTINGS.ble.enabled;
        break;
      case MenuAction::FRONTLIGHT:
        row.toggle = true;
        row.checked = Frontlight.isOn();
        break;
      default:
        break;
    }
  }
}
const ReaderChromeRow* EpubReaderMenuActivity::rowAt(int index) const {
  return index >= 0 && index < listCount() ? &menuRowItems[index] : nullptr;
}
