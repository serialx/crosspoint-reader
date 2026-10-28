#include "I18n.h"

#include <xp_ffi.h>

I18n& I18n::getInstance() {
  static I18n instance;
  return instance;
}

const char* I18n::get(StrId id) const {
  return xp_i18n_get(static_cast<uint8_t>(_language), static_cast<uint16_t>(id));
}

void I18n::setLanguage(Language lang) {
  _language =
      static_cast<Language>(xp_i18n_select_language(static_cast<uint8_t>(_language), static_cast<uint8_t>(lang)));
}

const char* I18n::getLanguageName(Language lang) const { return xp_i18n_language_name(static_cast<uint8_t>(lang)); }

Language I18n::languageFromCode(const char* code) { return static_cast<Language>(xp_i18n_language_from_code(code)); }

const char* I18n::getCharacterSet(Language lang) { return xp_i18n_character_set(static_cast<uint8_t>(lang)); }
