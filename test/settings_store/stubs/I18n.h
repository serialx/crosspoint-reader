#pragma once
#include <I18nKeys.h>
#include <cstring>
struct I18n {
  static Language languageFromCode(const char* code) {
    for (uint8_t i = 0; i < getLanguageCount(); ++i) if (strcmp(code, LANGUAGE_CODES[i]) == 0) return static_cast<Language>(i);
    return Language::EN;
  }
};
