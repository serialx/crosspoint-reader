#include "ClockSyncActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ClockSyncActivity::onEnter() {
  SettingsUiActivity::onEnter();
  if (!hasUiStorage()) return;
  selectIndex(0);
  syncedTime[0] = '\0';

  if (WiFi.status() == WL_CONNECTED) {
    requestUpdate();
    return;
  }

  shouldTearDownWifiOnExit = true;
  launchWifiSelection();
}

void ClockSyncActivity::onExit() {
  SettingsUiActivity::onExit();

  if (shouldTearDownWifiOnExit && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void ClockSyncActivity::launchWifiSelection() {
  LOG_INF("CLK", "Manual sync requested without WiFi, launching WiFi selection");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void ClockSyncActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    LOG_INF("CLK", "WiFi selection cancelled before manual clock sync");
    finish();
    return;
  }

  selectIndex(0);
  requestUpdate();
}

void ClockSyncActivity::runSync() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_INF("CLK", "Manual sync requested but WiFi is not connected after selection");
    selectIndex(xp_ui_settings_sync_result(0, 0));
    requestUpdate();
    return;
  }

  const bool ok = halClock.syncFromNTP();
  if (!ok) {
    selectIndex(xp_ui_settings_sync_result(1, 0));
    requestUpdate();
    return;
  }

  // Mark as synced so the auto-sync hook stops firing on future WiFi connects.
  SETTINGS.clockHasBeenSynced = 1;
  SETTINGS.saveToFile();

  // Read the freshly synced time back for the user-facing confirmation.
  char buf[9];
  if (halClock.formatTime(buf, sizeof(buf), SETTINGS.clockFormat == 1)) {
    snprintf(syncedTime, sizeof(syncedTime), "%s", buf);
  }
  selectIndex(xp_ui_settings_sync_result(1, 1));
  requestUpdate();
}

void ClockSyncActivity::loop() {
  XpUiInput input{};
  input.pressed = mappedInput.wasPressed(MappedInputManager::Button::Back) ? XP_UI_BACK : 0;
  int x = 0, y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    input.touch_flags = XP_UI_TOUCH_RELEASED;
    input.touch_x = x;
    input.touch_y = y;
  }
  const auto effect = xp_ui_settings_sync_update(selectedIndex(), &input);
  if (effect.kind == 4) {
    requestUpdateAndWait();
    runSync();
  } else if (effect.kind == XP_UI_CANCEL)
    finish();
}

const SettingsUiRow* ClockSyncActivity::rowAt(int index) const {
  if (index < 0 || index >= 4) return nullptr;
  static constexpr StrId titles[] = {StrId::STR_CLOCK_SYNCING, StrId::STR_CLOCK_SYNC_OK, StrId::STR_CLOCK_SYNC_NO_WIFI,
                                     StrId::STR_CLOCK_SYNC_FAIL};
  row = {};
  row.label = I18N.get(titles[index]);
  switch (index) {
    case 1:
      row.value = syncedTime[0] ? currentTime : nullptr;
      break;
    case 2:
      row.value = tr(STR_CLOCK_SYNC_NO_WIFI_HINT);
      break;
    case 3:
      row.value = tr(STR_CHECK_SERIAL_OUTPUT);
      break;
  }
  return &row;
}
void ClockSyncActivity::prepareRows() {
  snprintf(currentTime, sizeof(currentTime), "%s %s", tr(STR_CURRENT_TIME), syncedTime);
}
void ClockSyncActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
