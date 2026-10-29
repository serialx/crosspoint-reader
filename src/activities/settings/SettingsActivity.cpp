#include "SettingsActivity.h"

#include <BlePageTurner.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "AboutActivity.h"
#include "BleSettingsActivity.h"
#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "ClockSettingsActivity.h"
#include "CrossPointSettings.h"
#include "FontDownloadActivity.h"
#include "HomeButtonSettingsActivity.h"
#include "KOReaderSettingsActivity.h"
#include "KeyboardLayoutsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsList.h"
#include "SilentRestart.h"
#include "StatusBarSettingsActivity.h"
#include "TextSettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/PluginEvents.h"

namespace fui = freeink::ui;

namespace {
uint8_t categoryIdentity(const SettingInfo& setting) {
  if (setting.category == StrId::STR_CAT_SYSTEM) return 0;
  if (setting.category == StrId::STR_CAT_DISPLAY) return 1;
  if (setting.category == StrId::STR_CAT_READER) return 2;
  if (setting.category == StrId::STR_CAT_CONTROLS) return 3;
  return 4;
}
uint8_t fieldIdentity(const SettingInfo& setting) {
  const auto field = setting.valuePtr;
  if (field == &CrossPointSettings::libraryUseMetadata || field == &CrossPointSettings::removeReadBooksFromRecents ||
      field == &CrossPointSettings::moveFinishedToReadFolder)
    return 1;
  if (field == &CrossPointSettings::sleepTimeoutMinutes) return 2;
  if (field == &CrossPointSettings::fadingFix) return 3;
  if (field == &CrossPointSettings::pwrBtnFootnoteBack) return 4;
  if (home_button::isSetting(field)) return 5;
  return 0;
}
uint8_t capabilities() {
  return ((BoardConfig::isX4Pro() || BoardConfig::isX4Classic()) ? 1 : 0) | (halClock.isAvailable() ? 2 : 0) |
         (BoardConfig::isPicco() ? 4 : 0) | (BoardConfig::hasHomeKey() ? 8 : 0) | (BoardConfig::hasTouch() ? 16 : 0) |
         (bleturner::status().compiledIn ? 32 : 0);
}
SettingsSection sectionFor(const SettingInfo& setting) {
  return static_cast<SettingsSection>(xp_ui_settings_section(categoryIdentity(setting), fieldIdentity(setting)));
}
bool showOnDevice(const SettingInfo& setting) {
  return xp_ui_settings_visible(categoryIdentity(setting), fieldIdentity(setting), setting.inTextSettings,
                                capabilities(),
                                SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FOOTNOTES) != 0;
}

// Device-only action rows added per section, beyond the shared list.
constexpr size_t EXTRA_ROWS_PER_SECTION = 6;
}  // namespace

void SettingsActivity::rebuildSettings() {
  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran — otherwise the font-family picker shows stale list.
  sdFontSystem.refreshIfDirty();

  // Rescan /dictionaries on every rebuild: cheap (one directory listing) and
  // picks up dictionaries copied to the SD card since the last visit.
  std::vector<DictionaryEntry> dictionaries;
  DictionaryRegistry::discover(dictionaries);

  auto all = getSettingsList(&sdFontSystem.registry(), &dictionaries);

  const size_t count = std::count_if(all.begin(), all.end(), [this](const SettingInfo& setting) {
    return sectionFor(setting) == section && showOnDevice(setting);
  });
  settings.clear();
  settings.reserve(count + EXTRA_ROWS_PER_SECTION);

  static constexpr StrId actionLabels[] = {StrId::STR_NONE_OPT,
                                           StrId::STR_REMAP_FRONT_BUTTONS,
                                           StrId::STR_CUSTOMISE_STATUS_BAR,
                                           StrId::STR_CLOCK,
                                           StrId::STR_KOREADER_SYNC,
                                           StrId::STR_OPDS_SERVERS,
                                           StrId::STR_WIFI_NETWORKS,
                                           StrId::STR_BT_PAGE_TURNER,
                                           StrId::STR_CLEAR_READING_CACHE,
                                           StrId::STR_CHECK_UPDATES,
                                           StrId::STR_SD_FIRMWARE_UPDATE,
                                           StrId::STR_LANGUAGE,
                                           StrId::STR_MANAGE_FONTS,
                                           StrId::STR_TEXT_SETTINGS,
                                           StrId::STR_PLUGINS,
                                           StrId::STR_KEYBOARD_LAYOUTS,
                                           StrId::STR_HOME_BUTTON,
                                           StrId::STR_ABOUT};
  const uint8_t caps = capabilities();
  for (uint8_t i = 0;; ++i) {
    const auto action = xp_ui_settings_extra(static_cast<uint8_t>(section), caps, i);
    if (action == 0) break;
    if (action == 255) {
      for (auto& setting : all) {
        if (sectionFor(setting) == section && showOnDevice(setting)) settings.push_back(std::move(setting));
      }
    } else {
      settings.push_back(SettingInfo::Action(actionLabels[action], static_cast<SettingAction>(action)));
    }
  }
  rebuildRowItems();
}

SettingsActivity::SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const SettingsSection section)
    : SettingsUiActivity("Settings", renderer, mappedInput, XP_UI_SETTINGS_SECTION), section(section) {}

void SettingsActivity::onEnter() {
  SettingsUiActivity::onEnter();
  if (!hasUiStorage()) return;

  quickResume.preserve =
      SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  quickResume.automatic = false;
  syncQuickResumeTimeoutForSleepScreen(/*sleepScreenChanged=*/true, /*quickResumeTimeoutChanged=*/false);

  rebuildSettings();
}

// Rebuilds rowValues_/rowItems_ (label + actionValue) for settings.
// Structural — call only when the setting list changes, never from
// prepareRows(), which only refreshes rowValues_ content and rowItems_[].value
// pointers in place.
void SettingsActivity::rebuildRowItems() {
  rowValues_.assign(settings.size(), std::string());
  rowItems_.clear();
  rowItems_.reserve(settings.size());
  for (size_t i = 0; i < settings.size(); i++) {
    SettingsUiRow item;
    item.label = I18N.get(settings[i].nameId);
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

const char* SettingsActivity::headerTitle() const {
  return I18N.get(SETTINGS_SECTION_TITLES[static_cast<size_t>(section)]);
}

void SettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  // Most rows repaint a different surface (popup, sub-activity, new value);
  // a lingering tap flash would gray an unrelated element.

  selectIndex(index);
  toggleSelectedSetting();
  requestUpdate();
}

void SettingsActivity::applyUiSettingChange(uint8_t CrossPointSettings::* valuePtr) {
  // Theme changes take effect immediately, on this screen — reload the theme
  // and re-derive the app's tokens so the very next repaint is in the new look.
  if (valuePtr != &CrossPointSettings::uiTheme) {
    return;
  }
  UITheme::getInstance().reload();
  // Re-derive the shared tokens for the new look; the gate stays closed until
  // the repaint that rebuilds the interaction table in the new layout.
  resetUi();
}

bool SettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void SettingsActivity::toggleSelectedSetting() {
  mappedInput.resetHomeButtonInput();
  const int selectedSetting = selectedIndex();
  if (selectedSetting < 0 || selectedSetting >= static_cast<int>(settings.size())) {
    return;
  }

  const auto& setting = settings[selectedSetting];
  const bool sleepScreenChanged = setting.valuePtr == &CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged = setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen;

  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    openSleepTimeoutPicker();
    return;
  }

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = xp_ui_settings_edit(0, currentValue, 0, 0, 0, 0).value;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    const auto enumLabels = setting.enumLabels();
    const auto edit = xp_ui_settings_edit(1, currentValue, static_cast<uint8_t>(enumLabels.size()), 0, 0, 0);
    if (edit.kind == XP_UI_SETTINGS_POPUP) {
      const auto valuePtr = setting.valuePtr;
      optionPopup.show(setting.nameId, enumLabels.data(), static_cast<int>(enumLabels.size()), currentValue,
                       [this, valuePtr, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
                         SETTINGS.*valuePtr = idx;
                         syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
                         SETTINGS.saveToFile();
                         rebuildSettings();
                         applyUiSettingChange(valuePtr);
                       });
      requestUpdate();
      return;
    }
    if (edit.kind != XP_UI_SETTINGS_SAVE_VALUE) return;
    SETTINGS.*(setting.valuePtr) = edit.value;
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
    const uint8_t totalValues = setting.enumStringValues.empty()
                                    ? static_cast<uint8_t>(setting.enumLabels().size())
                                    : static_cast<uint8_t>(setting.enumStringValues.size());
    const uint8_t cur = setting.valueGetter();
    const auto edit = xp_ui_settings_edit(1, cur, totalValues, 0, 0, 0);
    if (edit.kind == XP_UI_SETTINGS_POPUP) {
      const auto valueSetter = setting.valueSetter;
      auto onSelect = [this, valueSetter, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
        valueSetter(idx);
        syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
        SETTINGS.saveToFile();
        rebuildSettings();
      };
      if (!setting.enumStringValues.empty()) {
        optionPopup.show(setting.nameId, setting.enumStringValues, cur, std::move(onSelect));
      } else {
        const auto enumLabels = setting.enumLabels();
        optionPopup.show(setting.nameId, enumLabels.data(), static_cast<int>(enumLabels.size()), cur,
                         std::move(onSelect));
      }
      requestUpdate();
      return;
    }
    if (edit.kind != XP_UI_SETTINGS_SAVE_VALUE) return;
    setting.valueSetter(edit.value);
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    SETTINGS.*setting.valuePtr = xp_ui_settings_edit(3, SETTINGS.*setting.valuePtr, 0, setting.valueRange.min,
                                                     setting.valueRange.max, setting.valueRange.step)
                                     .value;
  } else if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };

    switch (setting.action) {
      case SettingAction::HomeButton: {
        // Activities must outlive this call and are owned by the activity stack.
        auto activity = makeUniqueNoThrow<HomeButtonSettingsActivity>(renderer, mappedInput);
        if (!activity) {
          LOG_ERR("SET", "OOM: Home button settings");
          return;
        }
        startActivityForResult(std::move(activity), [this](const ActivityResult&) { requestUpdate(); });
        return;
      }
      case SettingAction::RemapFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::ClockSettings:
        if (auto activity = makeUniqueNoThrow<ClockSettingsActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), resultHandler);
        } else {
          LOG_ERR("SETTINGS", "OOM: ClockSettingsActivity");
        }
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(std::make_unique<OpdsServerListActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Network: {
        auto activity = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, false);
        if (!activity) {
          LOG_ERR("SETTINGS", "OOM: WifiSelectionActivity");
          return;
        }
        startActivityForResult(std::move(activity), [](const ActivityResult&) {
          SETTINGS.saveToFile();
          // Every other WiFi consumer hands the radio to a session it owns;
          // these rows only save credentials, so nothing here would ever
          // release the driver's heap. The scan alone brings it up, so tear
          // down whether or not the user joined a network.
          if (WiFi.getMode() == WIFI_MODE_NULL) return;
          WiFi.disconnect(false);
          delay(30);
          // Unlike the onExit() teardowns, this runs from the loop task with
          // no lock held; the restart popup paints straight to the panel.
          RenderLock lock;
          silentRestartToSettings();
        });
        break;
      }
      case SettingAction::Bluetooth: {
        auto activity = makeUniqueNoThrow<BleSettingsActivity>(renderer, mappedInput);
        if (!activity) {
          LOG_ERR("SETTINGS", "OOM: BleSettingsActivity");
          return;
        }
        startActivityForResult(std::move(activity), resultHandler);
        break;
      }
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CheckForUpdates:
        startActivityForResult(std::make_unique<OtaUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::SdFirmwareUpdate:
        startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::DownloadFonts:
        startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettings();
                               });
        break;
      case SettingAction::TextSettings:
        startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                      TextSettingsActivity::Tab::Family),
                               [this](const ActivityResult&) {
                                 // TextSettingsActivity saves on each change; no save needed here.
                                 rebuildSettings();
                               });
        break;
      case SettingAction::Language:
        // Row labels are translated once in rebuildRowItems() and don't
        // re-run on Pop (see ActivityManager::loop()), so a language switch
        // needs an explicit rebuild here rather than the generic resultHandler.
        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettings();
                               });
        break;
      case SettingAction::Plugins: {
        static constexpr StrId OPTIONS[] = {StrId::STR_ENABLED, StrId::STR_DISABLED, StrId::STR_CANCEL};
        optionPopup.show(StrId::STR_PLUGINS, OPTIONS, 3, SETTINGS.pluginsEnabled ? 0 : 1, [this](const int idx) {
          if (idx == 2) return;
          SETTINGS.pluginsEnabled = idx == 0 ? 1 : 0;
          // Turning plugins on brings back a dismissed Plugin Hub install row.
          if (SETTINGS.pluginsEnabled) SETTINGS.pluginHubPromptHidden = 0;
          SETTINGS.saveToFile();
          // Applies to event delivery now, not after a reboot.
          pluginevents::refreshSubscriptions();
          rebuildSettings();
        });
        requestUpdate();
        return;
      }
      case SettingAction::KeyboardLayouts:
        if (auto activity = makeUniqueNoThrow<KeyboardLayoutsActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), nullptr);
        } else {
          LOG_ERR("SETTINGS", "OOM: KeyboardLayoutsActivity");
        }
        break;
      case SettingAction::About:
        if (auto activity = makeUniqueNoThrow<AboutActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), nullptr);
        } else {
          LOG_ERR("SETTINGS", "OOM: AboutActivity");
        }
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else {
    return;
  }

  syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
  SETTINGS.saveToFile();
  const auto valuePtr = setting.valuePtr;  // rebuildSettings() replaces `setting`
  rebuildSettings();
  applyUiSettingChange(valuePtr);
  // Rows can appear/disappear with a change (e.g. the footnote-back toggle).
  selectIndex(std::min(selectedSetting, static_cast<int>(settings.size()) - 1));
}

void SettingsActivity::syncQuickResumeTimeoutForSleepScreen(bool screenChanged, bool timeoutChanged) {
  SETTINGS.quickResumeSleepScreen = xp_ui_settings_quick_resume(
      &quickResume, SETTINGS.quickResumeSleepScreen,
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME, screenChanged, timeoutChanged);
}

void SettingsActivity::openSleepTimeoutPicker() {
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(
          renderer, mappedInput, "SleepTimeoutInterval", StrId::STR_TIME_TO_SLEEP, SETTINGS.sleepTimeoutMinutes,
          CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1, 5,
          StrId::STR_SLEEP_TIMER_VALUE_FORMAT, false, StrId::STR_SLEEP_NEVER),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

std::string SettingsActivity::settingValueText(const SettingInfo& setting) {
  if (setting.action == SettingAction::HomeButton) return tr(STR_CONFIGURE);
  if (setting.action == SettingAction::Plugins) return SETTINGS.pluginsEnabled ? tr(STR_ENABLED) : tr(STR_DISABLED);
  if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    // Guard like the valueGetter branch below: a corrupt/migrated settings
    // byte must not index past the enum table.
    const uint8_t value = SETTINGS.*(setting.valuePtr);
    const auto enumLabels = setting.enumLabels();
    if (value >= enumLabels.size()) return "";
    return I18N.get(enumLabels[value]);
  }
  if (setting.type == SettingType::ENUM && setting.valueGetter) {
    const uint8_t value = setting.valueGetter();
    if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
      return setting.enumStringValues[value];
    }
    const auto enumLabels = setting.enumLabels();
    if (value < enumLabels.size()) {
      return I18N.get(enumLabels[value]);
    }
    return "";
  }
  if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
      if (SETTINGS.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) {
        return tr(STR_SLEEP_NEVER);
      }
      char valueBuffer[32];
      snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
               static_cast<unsigned int>(SETTINGS.*(setting.valuePtr)));
      return valueBuffer;
    }
    return std::to_string(SETTINGS.*(setting.valuePtr));
  }
  return "";
}

void SettingsActivity::prepareRows() {
  for (size_t i = 0; i < settings.size(); i++) {
    const auto& setting = settings[i];
    const auto labels = setting.enumLabels();
    const bool checkbox = setting.type == SettingType::TOGGLE ||
                          (setting.type == SettingType::ENUM && setting.enumStringValues.empty() &&
                           labels.size() == 2 && labels[0] == StrId::STR_STATE_OFF && labels[1] == StrId::STR_STATE_ON);
    if (checkbox && (setting.valuePtr || setting.valueGetter)) {
      const bool checked = setting.valuePtr ? SETTINGS.*(setting.valuePtr) != 0 : setting.valueGetter() != 0;
      rowValues_[i].clear();
      setCheckboxRow(rowItems_[i], checked);
    } else {
      rowValues_[i] = settingValueText(setting);
      rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
    }
  }
}

void SettingsActivity::drawFooter() {
  const int selected = selectedIndex();
  const bool picker =
      selected >= 0 && selected < static_cast<int>(settings.size()) &&
      (settings[selected].type == SettingType::ACTION || settings[selected].nameId == StrId::STR_TIME_TO_SLEEP);
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), picker ? tr(STR_SELECT) : tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void SettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  SettingsUiActivity::render(std::move(lock));
}
