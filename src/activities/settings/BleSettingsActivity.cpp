#include "BleSettingsActivity.h"

#include <BlePageTurner.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>
#include <iterator>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId MAP_LABELS[] = {StrId::STR_NEXT_PAGE,       StrId::STR_PREV_PAGE,    StrId::STR_BT_NEXT_CHAPTER,
                                StrId::STR_BT_PREV_CHAPTER, StrId::STR_CONFIRM,      StrId::STR_BACK,
                                StrId::STR_READER_MENU,     StrId::STR_SAVE_CLIPPING};

}  // namespace

void BleSettingsActivity::onEnter() {
  SettingsUiActivity::onEnter();
  if (!hasUiStorage()) return;
  // A saved opt-in brings the radio up here, so scanning and pairing work at once.
  if (SETTINGS.ble.enabled) bleturner::switchOn();
  refresh();
  requestUpdate();
}

void BleSettingsActivity::loop() {
  const bool blocked = consumeRejectedInput();
  if (!blocked && state.view == XP_UI_SETTINGS_BLE_VIEW_MAPPING &&
      mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 700)) {
    clearMapping(selectedIndex());
  }
  if (!blocked) SettingsUiActivity::loop();
  if (!xp_ui_settings_ble_poll(&state, millis())) return;
  // A remote being paired that links becomes the chosen one: save it.
  if (SETTINGS.ble.enabled && bleturner::service()) SETTINGS.saveToFile();
  if (state.action != 0) stepLearning();
  if (signature() == state.shown_signature) return;  // an e-ink repaint only when something changed
  refresh();
  requestUpdate();
}

const char* BleSettingsActivity::headerTitle() const {
  static constexpr StrId titles[] = {StrId::STR_BT_PAGE_TURNER, StrId::STR_BT_SCAN, StrId::STR_BT_PAIRED_REMOTES,
                                     StrId::STR_BT_MAP_BUTTONS};
  return I18N.get(titles[state.view < 4 ? state.view : 0]);
}
void BleSettingsActivity::showView(uint8_t target) {
  {
    RenderLock lock(*this);
    if (rustSceneRejected()) {
      requestUpdate();
      return;
    }
    xp_ui_settings_ble_show(&state, target);
    resetSelection();
  }
  refresh();
  requestUpdate();
}
void BleSettingsActivity::onBackButton() {
  const auto effect = xp_ui_settings_ble_back(&state);
  if (effect == 2) {
    finish();
    return;
  }
  if (effect == 4) bleturner::scan(0);
  if (effect >= 3) resetSelection();
  refresh();
  requestUpdate();
}
const char* BleSettingsActivity::learningNote() const {
  static constexpr StrId notes[] = {StrId::STR_BT_CONNECT_REMOTE, StrId::STR_BT_NO_KEY, StrId::STR_BT_KEYS_NOT_SAVED,
                                    StrId::STR_BT_MAPPING_SAVED, StrId::STR_BT_MAPPING_CLEARED};
  return state.note > 0 && state.note <= std::size(notes) ? I18N.get(notes[state.note - 1]) : nullptr;
}

uint32_t BleSettingsActivity::signature() const {
  const auto st = bleturner::status();
  return (st.running ? 1u : 0u) | (st.scanning ? 2u : 0u) | (st.connected ? 4u : 0u) | (st.connecting ? 8u : 0u) |
         (st.stopping ? 16u : 0u) | (SETTINGS.ble.enabled ? 32u : 0u) |
         static_cast<uint32_t>(bleturner::bondCount()) << 8 | static_cast<uint32_t>(bleturner::foundCount()) << 16;
}

void BleSettingsActivity::refresh() {
  RenderLock lock(*this);
  state.shown_signature = signature();
  const auto st = bleturner::status();
  char failure[48];
  const uint8_t flags = static_cast<uint8_t>(state.shown_signature);
  const bool failed = SETTINGS.ble.enabled && !st.stopping && bleturner::takeConnectFailure(failure, sizeof(failure));
  switch (xp_ui_settings_ble_status(flags, failed)) {
    case 0:
      status = tr(STR_STATE_OFF);
      break;
    case 1:
      status = failure;
      break;
    case 2:
      status = tr(STR_BT_START_FAILED);
      break;
    case 3:
      status = bleturner::linked().name;
      break;
    case 4:
      status = tr(STR_CONNECTING);
      break;
    case 5:
      status = tr(STR_SCANNING);
      break;
    default:
      status = tr(STR_STATE_ON);
      break;
  }

  // Labels first: the rows point into them.
  const uint8_t found = st.running ? bleturner::foundCount() : 0;
  const uint8_t bonds = st.running ? bleturner::bondCount() : 0;
  labels.clear();
  for (uint8_t i = 0; i < found; ++i) labels.emplace_back(bleturner::found(i).name);
  for (uint8_t i = 0; i < bonds; ++i) {
    const auto peer = bleturner::bond(i);
    labels.emplace_back(peer.name[0] != '\0' ? peer.name : peer.addr);
  }

  rows.clear();
  static constexpr StrId fixed[] = {StrId::STR_BT_PAGE_TURNER, StrId::STR_BT_SCAN,      StrId::STR_BT_PAIRED_REMOTES,
                                    StrId::STR_BT_MAP_BUTTONS, StrId::STR_BT_STOP_SCAN, StrId::STR_SCANNING,
                                    StrId::STR_BT_NO_DEVICES,  StrId::STR_BT_NO_REMOTES};
  const auto peer = bleturner::linked();
  const auto* table = state.view == XP_UI_SETTINGS_BLE_VIEW_MAPPING
                          ? bleturner::tableFor(SETTINGS.ble.remotes, SETTINGS.ble.remoteCount, peer.addr, peer.name)
                          : nullptr;
  XpUiSettingsBleRow spec{};
  for (uint16_t i = 0; xp_ui_settings_ble_row(&state, flags, found, bonds, i, &spec); ++i) {
    SettingsUiRow row;
    row.actionValue = spec.code;
    row.enabled = spec.enabled;
    row.toggle = spec.toggle;
    row.toggleChecked = spec.checked;
    if (spec.label < 8)
      row.label = I18N.get(fixed[spec.label]);
    else if (spec.label < 16)
      row.label = I18N.get(MAP_LABELS[spec.label - 8]);
    else
      row.label = labels[spec.label - 16].c_str();
    switch (spec.subtitle) {
      case 1:
        row.subtitle = status.c_str();
        break;
      case 2:
        row.subtitle = tr(STR_BT_CONNECT_REMOTE);
        break;
      case 3:
        row.subtitle = tr(STR_BT_PRESS_REMOTE);
        break;
      case 4:
        row.subtitle = learningNote();
        break;
    }
    if (spec.value == 1)
      row.value = tr(STR_FORGET_BUTTON);
    else if (spec.value == 2) {
      bleturner::Binding binding;
      if (table && bleturner::find(*table, static_cast<bleturner::Action>(xp_ui_settings_ble_action(i)), binding)) {
        bleturner::formatCode(mappingValues[i], sizeof(mappingValues[i]), binding);
        if (binding & bleturner::kHoldBit) {
          const size_t used = strlen(mappingValues[i]);
          snprintf(mappingValues[i] + used, sizeof(mappingValues[i]) - used, " %s", tr(STR_BT_HOLD));
        }
        row.value = mappingValues[i];
      } else
        row.value = tr(STR_NOT_SET);
    }
    rows.push_back(row);
  }
  if (selectedIndex() >= static_cast<int>(rows.size())) selectIndex(static_cast<int>(rows.size()) - 1);
}

void BleSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(rows.size()) || !rows[index].enabled) return;
  selectIndex(index);
  const auto effect =
      xp_ui_settings_ble_activate(&state, rows[index].actionValue, SETTINGS.ble.enabled, bleturner::status().scanning);
  switch (effect.kind) {
    case XP_UI_SETTINGS_BLE_EFFECT_TOGGLE:
      SETTINGS.ble.enabled = effect.value;
      if (SETTINGS.ble.enabled)
        bleturner::switchOn();
      else
        bleturner::switchOff();
      SETTINGS.saveToFile();
      break;
    case XP_UI_SETTINGS_BLE_EFFECT_SCAN:
      bleturner::scan(effect.value);
      if (state.view == XP_UI_SETTINGS_BLE_VIEW_MENU) {
        showView(XP_UI_SETTINGS_BLE_VIEW_SCAN);
        return;
      }
      break;
    case XP_UI_SETTINGS_BLE_EFFECT_SHOW:
      showView(effect.value);
      return;
    case XP_UI_SETTINGS_BLE_EFFECT_PAIR: {
      const auto peer = bleturner::found(effect.value);
      const std::string addr = peer.addr, name = peer.name;
      bleturner::scan(0);
      bleturner::pair(addr.c_str(), name.c_str());
      showView(XP_UI_SETTINGS_BLE_VIEW_MENU);
      return;
    }
    case XP_UI_SETTINGS_BLE_EFFECT_FORGET: {
      const std::string addr = bleturner::bond(effect.value).addr;
      if (bleturner::forget(addr.c_str())) SETTINGS.saveToFile();
      break;
    }
    case XP_UI_SETTINGS_BLE_EFFECT_LEARN:
      startLearning(effect.value);
      break;
  }
  refresh();
  requestUpdate();
}

void BleSettingsActivity::startLearning(const int index) {
  if (index < 0 || index >= static_cast<int>(std::size(MAP_LABELS)) || !bleturner::status().connected) return;
  bleturner::Event ev;
  while (bleturner::pollEvent(ev)) {
  }
  xp_ui_settings_ble_begin(&state, index, true, millis());
  const auto peer = bleturner::linked();
  strncpy(learnAddr, peer.addr, sizeof(learnAddr) - 1);
}
void BleSettingsActivity::stepLearning() {
  const auto peer = bleturner::linked();
  if (!bleturner::status().connected || strcmp(peer.addr, learnAddr) != 0) {
    xp_ui_settings_ble_step(&state, XP_UI_SETTINGS_BLE_EVENT_DISCONNECTED, 0, 0, millis());
    refresh();
    requestUpdate();
    return;
  }
  const auto action = state.action;
  bleturner::Event ev;
  while (state.action == action && bleturner::pollEvent(ev)) {
    const auto result = xp_ui_settings_ble_step(&state,
                                                ev.pressed   ? XP_UI_SETTINGS_BLE_EVENT_PRESS
                                                : ev.wasRest ? XP_UI_SETTINGS_BLE_EVENT_REST
                                                             : XP_UI_SETTINGS_BLE_EVENT_RELEASE,
                                                ev.code, ev.atMs, millis());
    if (result == 1 || result == 2) keepLearned(result == 2);
  }
  if (state.action == action) {
    const auto result = xp_ui_settings_ble_step(&state, XP_UI_SETTINGS_BLE_EVENT_TICK, 0, 0, millis());
    if (result == 1) keepLearned(false);
  }
  if (state.action != action) {
    refresh();
    requestUpdate();
  }
}

void BleSettingsActivity::keepLearned(const bool hold) {
  const auto peer = bleturner::linked();
  if (!bleturner::status().connected || strcmp(peer.addr, learnAddr) != 0) {
    xp_ui_settings_ble_step(&state, XP_UI_SETTINGS_BLE_EVENT_DISCONNECTED, 0, 0, millis());
    return;
  }
  auto* table = bleturner::editableTable(SETTINGS.ble.remotes, SETTINGS.ble.remoteCount, peer.addr, peer.name);
  if (table == nullptr || !bleturner::learn(*table, static_cast<bleturner::Action>(state.action), state.code, hold)) {
    xp_ui_settings_ble_step(&state, XP_UI_SETTINGS_BLE_EVENT_SAVE_FAILED, 0, 0, millis());
    return;
  }
  SETTINGS.saveToFile();
  xp_ui_settings_ble_step(&state, XP_UI_SETTINGS_BLE_EVENT_SAVED, 0, 0, millis());
}
void BleSettingsActivity::onRowLongPress(const int index) {
  if (state.view == XP_UI_SETTINGS_BLE_VIEW_MAPPING) clearMapping(index);
}
void BleSettingsActivity::clearMapping(const int index) {
  const auto action = static_cast<bleturner::Action>(xp_ui_settings_ble_clear(&state, index));
  if (action == bleturner::Action::None) return;
  if (bleturner::status().connected) {
    const auto peer = bleturner::linked();
    auto* table = bleturner::editableTable(SETTINGS.ble.remotes, SETTINGS.ble.remoteCount, peer.addr, peer.name);
    if (table != nullptr) bleturner::clearAction(*table, action);
  }
  if (action == bleturner::Action::NextPage) SETTINGS.ble.nextKeyUsage = bleturner::kUsageNone;
  if (action == bleturner::Action::PrevPage) SETTINGS.ble.prevKeyUsage = bleturner::kUsageNone;
  SETTINGS.saveToFile();
  refresh();
  requestUpdate();
}
void BleSettingsActivity::prepareRows() {}
