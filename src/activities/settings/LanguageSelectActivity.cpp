#include "LanguageSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <iterator>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

LanguageSelectActivity::LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : SettingsUiActivity("LanguageSelect", renderer, mappedInput, XP_UI_SETTINGS_LANGUAGE) {}

void LanguageSelectActivity::onEnter() {
  SettingsUiActivity::onEnter();
  if (!hasUiStorage()) return;

  // Open on the current language, which may sit past the first page; the first
  // screen build pulls the viewport to it (ListNav follow-on-build).
  const auto currentLang = static_cast<uint8_t>(I18N.getLanguage());
  const auto* begin = std::begin(SORTED_LANGUAGE_INDICES);
  const auto* end = std::end(SORTED_LANGUAGE_INDICES);
  const auto* it = std::find(begin, end, currentLang);
  selectIndex((it != end) ? static_cast<int>(std::distance(begin, it)) : 0);

  // Built once here rather than every prepareRows() call: labels are static,
  // and the "Selected" marker can't go stale mid-visit since activateIndex()
  // finishes the activity immediately on selection.
  for (int i = 0; i < totalItems; ++i) {
    SettingsUiRow item;
    item.label = I18N.getLanguageName(static_cast<Language>(SORTED_LANGUAGE_INDICES[i]));
    if (SORTED_LANGUAGE_INDICES[i] == currentLang) {
      item.value = tr(STR_SELECTED);
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems[i] = item;
  }
}

const char* LanguageSelectActivity::headerTitle() const { return tr(STR_LANGUAGE); }

void LanguageSelectActivity::activateIndex(const int index) {
  // The activated row leaves this screen; a lingering flash would gray an
  // unrelated element on the next render.

  selectIndex(index);
  const uint8_t langIndex = SORTED_LANGUAGE_INDICES[index];

  {
    RenderLock lock(*this);
    I18N.setLanguage(static_cast<Language>(langIndex));
  }

  SETTINGS.language = langIndex;
  SETTINGS.saveToFile();

  // Return to previous page
  finish();
}

void LanguageSelectActivity::prepareRows() {}
