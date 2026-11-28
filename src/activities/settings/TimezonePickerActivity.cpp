#include "TimezonePickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "util/Timezones.h"

namespace fui = freeink::ui;

TimezonePickerActivity::TimezonePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : SettingsUiActivity("TimezonePicker", renderer, mappedInput, XP_UI_SETTINGS_TIMEZONE) {}

void TimezonePickerActivity::onEnter() {
  SettingsUiActivity::onEnter();
  if (!hasUiStorage()) return;
  const size_t count = timezones::count();
  offsetLabels_.resize(count);
  rowItems_.resize(count);
  for (size_t i = 0; i < count; i++) {
    char offset[12];
    timezones::formatOffset(static_cast<uint8_t>(i), offset, sizeof(offset));
    offsetLabels_[i] = offset;
    rowItems_[i].label = timezones::table()[i].name;
    rowItems_[i].value = offsetLabels_[i].c_str();
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
  // Open on the active zone rather than the top of a ~50-row list.
  resetSelection(timezones::activeIndex());
}

int TimezonePickerActivity::listCount() const { return static_cast<int>(timezones::count()); }

const char* TimezonePickerActivity::headerTitle() const { return tr(STR_TIMEZONE); }

void TimezonePickerActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  SETTINGS.clockTimezone = static_cast<uint8_t>(index);
  SETTINGS.saveToFile();
  timezones::applyToClock();
  finish();
}

void TimezonePickerActivity::prepareRows() {}
