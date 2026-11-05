#include "SettingsListActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "SettingsActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

SettingsListActivity::SettingsListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const SettingsSection openSection)
    : SettingsUiActivity("SettingsList", renderer, mappedInput, XP_UI_SETTINGS_ROOT), pendingSection(openSection) {}

void SettingsListActivity::onEnter() {
  SettingsUiActivity::onEnter();
  if (!hasUiStorage()) return;
  rebuildRows();
}

void SettingsListActivity::onExit() {
  SettingsUiActivity::onExit();
  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsListActivity::loop() {
  if (pendingSection != SettingsSection::End) {
    const SettingsSection section = pendingSection;
    pendingSection = SettingsSection::End;
    openSection(section);
    return;
  }
  SettingsUiActivity::loop();
}

// Rebuilds row labels; call when entering and after a section returns (the
// UI language may have changed).
void SettingsListActivity::rebuildRows() {
  for (size_t i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
    auto& item = rowItems_[i];
    item.label = I18N.get(SETTINGS_SECTION_TITLES[i]);
    item.icon = static_cast<uint32_t>(i + 1);
    item.actionValue = static_cast<int16_t>(i);
  }
}

void SettingsListActivity::prepareRows() {}

void SettingsListActivity::activateIndex(const int index) {
  // The section screen replaces this one; a lingering flash would gray an
  // unrelated element on return.

  openSection(static_cast<SettingsSection>(index));
}

void SettingsListActivity::openSection(const SettingsSection section) {
  selectIndex(static_cast<int>(section));
  auto activity = makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, section);
  if (!activity) {
    LOG_ERR("SETTINGS", "OOM: SettingsActivity");
    return;
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    rebuildRows();
    // A theme change inside the section must restyle this screen too.
    resetUi();
    requestUpdate();
  });
}

void SettingsListActivity::onBackButton() { onGoHome(); }

const char* SettingsListActivity::headerTitle() const { return tr(STR_SETTINGS_TITLE); }

const char* SettingsListActivity::headerTrailing() const { return CROSSPOINT_VERSION; }
