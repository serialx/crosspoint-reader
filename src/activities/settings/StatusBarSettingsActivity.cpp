#include "StatusBarSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <cstring>
#include <memory>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// Menu items in their natural order. The clock position entry is appended only
// when the RTC probe found hardware; time, zone, and format live in Settings >
// System > Clock.
enum MenuItem {
  ITEM_CHAPTER_PAGE_COUNT = 0,
  ITEM_BOOK_PROGRESS_PERCENTAGE,
  ITEM_PROGRESS_BAR,
  ITEM_PROGRESS_BAR_THICKNESS,
  ITEM_TITLE,
  ITEM_BATTERY,
  ITEM_XTC_STATUS_BAR,
  ITEM_CLOCK,  // RTC boards only
  ITEM_COUNT
};

constexpr int BASE_MENU_ITEMS = ITEM_CLOCK;  // Items shown on every device
constexpr int FULL_MENU_ITEMS = ITEM_COUNT;  // Items shown when RTC is available
static_assert(FULL_MENU_ITEMS == StatusBarSettingsActivity::MAX_STATUS_BAR_ITEMS,
              "keep StatusBarSettingsActivity::MAX_STATUS_BAR_ITEMS in sync with ITEM_COUNT");

const StrId menuNames[FULL_MENU_ITEMS] = {
    StrId::STR_CHAPTER_PAGE_COUNT,
    StrId::STR_BOOK_PROGRESS_PERCENTAGE,
    StrId::STR_PROGRESS_BAR,
    StrId::STR_PROGRESS_BAR_THICKNESS,
    StrId::STR_TITLE,
    StrId::STR_BATTERY,
    StrId::STR_XTC_STATUS_BAR,
    StrId::STR_CLOCK,
};

constexpr int PROGRESS_BAR_ITEMS = 3;
const StrId progressBarNames[PROGRESS_BAR_ITEMS] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};

constexpr int PROGRESS_BAR_THICKNESS_ITEMS = 3;
const StrId progressBarThicknessNames[PROGRESS_BAR_THICKNESS_ITEMS] = {
    StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK};

constexpr int TITLE_ITEMS = 3;
const StrId titleNames[TITLE_ITEMS] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};

constexpr int XTC_STATUS_BAR_ITEMS = 3;
const StrId xtcStatusBarNames[XTC_STATUS_BAR_ITEMS] = {StrId::STR_HIDE, StrId::STR_BOTTOM, StrId::STR_TOP};

constexpr int STATUS_BAR_CLOCK_ITEMS = CrossPointSettings::STATUS_BAR_CLOCK_MODE_COUNT;
const StrId statusBarClockNames[STATUS_BAR_CLOCK_ITEMS] = {StrId::STR_HIDE, StrId::STR_DIR_RIGHT, StrId::STR_DIR_LEFT};

const int verticalPreviewTextPadding = 40;
}  // namespace

StatusBarSettingsActivity::StatusBarSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : SettingsUiActivity("StatusBarSettings", renderer, mappedInput, XP_UI_SETTINGS_STATUS_BAR) {}

void StatusBarSettingsActivity::onEnter() {
  SettingsUiActivity::onEnter();
  if (!hasUiStorage()) return;

  visibleItemCount = halClock.isAvailable() ? FULL_MENU_ITEMS : BASE_MENU_ITEMS;

  SETTINGS.statusBarProgressBar = xp_ui_settings_status_value(ITEM_PROGRESS_BAR, SETTINGS.statusBarProgressBar);
  SETTINGS.statusBarProgressBarThickness =
      xp_ui_settings_status_value(ITEM_PROGRESS_BAR_THICKNESS, SETTINGS.statusBarProgressBarThickness);
  SETTINGS.statusBarTitle = xp_ui_settings_status_value(ITEM_TITLE, SETTINGS.statusBarTitle);
  SETTINGS.xtcStatusBarMode = xp_ui_settings_status_value(ITEM_XTC_STATUS_BAR, SETTINGS.xtcStatusBarMode);
  SETTINGS.statusBarClock = xp_ui_settings_status_value(ITEM_CLOCK, SETTINGS.statusBarClock);

  // Labels never change (unlike the values, which track live SETTINGS
  // state), so they're set once here rather than every prepareRows() call.
  for (int i = 0; i < visibleItemCount; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

bool StatusBarSettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void StatusBarSettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  selectIndex(index);
  // Activation opens a popup/sub-activity or repaints a new value; a lingering
  // flash would gray an unrelated row.

  handleSelection();
  requestUpdate();
}

void StatusBarSettingsActivity::handleSelection() {
  switch (selectedIndex()) {
    case ITEM_CHAPTER_PAGE_COUNT:
      SETTINGS.statusBarChapterPageCount =
          xp_ui_settings_menu_edit(XP_UI_SETTINGS_STATUS_BAR, selectedIndex(), SETTINGS.statusBarChapterPageCount)
              .value;
      break;
    case ITEM_BOOK_PROGRESS_PERCENTAGE:
      SETTINGS.statusBarBookProgressPercentage =
          xp_ui_settings_menu_edit(XP_UI_SETTINGS_STATUS_BAR, selectedIndex(), SETTINGS.statusBarBookProgressPercentage)
              .value;
      break;
    case ITEM_PROGRESS_BAR:
      optionPopup.show(StrId::STR_PROGRESS_BAR, progressBarNames, PROGRESS_BAR_ITEMS, SETTINGS.statusBarProgressBar,
                       [this](int idx) {
                         SETTINGS.statusBarProgressBar = idx;
                         SETTINGS.saveToFile();
                       });
      return;
    case ITEM_PROGRESS_BAR_THICKNESS:
      optionPopup.show(StrId::STR_PROGRESS_BAR_THICKNESS, progressBarThicknessNames, PROGRESS_BAR_THICKNESS_ITEMS,
                       SETTINGS.statusBarProgressBarThickness, [this](int idx) {
                         SETTINGS.statusBarProgressBarThickness = idx;
                         SETTINGS.saveToFile();
                       });
      return;
    case ITEM_TITLE:
      optionPopup.show(StrId::STR_TITLE, titleNames, TITLE_ITEMS, SETTINGS.statusBarTitle, [this](int idx) {
        SETTINGS.statusBarTitle = idx;
        SETTINGS.saveToFile();
      });
      return;
    case ITEM_BATTERY:
      SETTINGS.statusBarBattery =
          xp_ui_settings_menu_edit(XP_UI_SETTINGS_STATUS_BAR, selectedIndex(), SETTINGS.statusBarBattery).value;
      break;
    case ITEM_XTC_STATUS_BAR:
      optionPopup.show(StrId::STR_XTC_STATUS_BAR, xtcStatusBarNames, XTC_STATUS_BAR_ITEMS, SETTINGS.xtcStatusBarMode,
                       [this](int idx) {
                         SETTINGS.xtcStatusBarMode = idx;
                         SETTINGS.saveToFile();
                       });
      return;
    case ITEM_CLOCK:
      SETTINGS.statusBarClock =
          xp_ui_settings_menu_edit(XP_UI_SETTINGS_STATUS_BAR, selectedIndex(), SETTINGS.statusBarClock).value;
      break;
    default:
      return;
  }
  SETTINGS.saveToFile();
}

std::string StatusBarSettingsActivity::rowValueText(const int index) {
  switch (index) {
    case ITEM_PROGRESS_BAR:
      return I18N.get(progressBarNames[SETTINGS.statusBarProgressBar]);
    case ITEM_PROGRESS_BAR_THICKNESS:
      return I18N.get(progressBarThicknessNames[SETTINGS.statusBarProgressBarThickness]);
    case ITEM_TITLE:
      return I18N.get(titleNames[SETTINGS.statusBarTitle]);
    case ITEM_XTC_STATUS_BAR:
      return I18N.get(xtcStatusBarNames[SETTINGS.xtcStatusBarMode]);
    case ITEM_CLOCK:
      return I18N.get(statusBarClockNames[SETTINGS.statusBarClock]);
    default:
      return "";
  }
}

void StatusBarSettingsActivity::prepareRows() {
  for (int i = 0; i < visibleItemCount; i++) {
    rowValues_[i] = rowValueText(i);
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }
  setCheckboxRow(rowItems_[ITEM_CHAPTER_PAGE_COUNT], SETTINGS.statusBarChapterPageCount);
  setCheckboxRow(rowItems_[ITEM_BOOK_PROGRESS_PERCENTAGE], SETTINGS.statusBarBookProgressPercentage);
  setCheckboxRow(rowItems_[ITEM_BATTERY], SETTINGS.statusBarBattery);
}

void StatusBarSettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  SettingsUiActivity::render(std::move(lock));
}
void StatusBarSettingsActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
void StatusBarSettingsActivity::drawPreview() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  std::string title;
  if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = tr(STR_EXAMPLE_BOOK);
  } else if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_EXAMPLE_CHAPTER);
  }

  // Anchor the preview as a footer directly above the button hints.
  GUI.drawStatusBar(renderer, 75, 8, 32, title, metrics.buttonHintsHeight, 0, false);

  renderer.drawCenteredText(UI_10_FONT_ID,
                            renderer.getScreenHeight() - UITheme::getInstance().getStatusBarHeight() -
                                metrics.buttonHintsHeight - verticalPreviewTextPadding,
                            tr(STR_PREVIEW));
}
