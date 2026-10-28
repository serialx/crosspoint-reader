#include "I18n.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

static void emit(const char* value) {
  const auto size = static_cast<unsigned>(strlen(value));
  printf("%u:", size);
  fwrite(value, 1, size, stdout);
  putchar('\n');
}

int main() {
  auto& i18n = I18n::getInstance();
  printf("%u %u\n", getLanguageCount(), static_cast<unsigned>(StrId::_COUNT));
  for (unsigned lang = 0; lang < getLanguageCount(); ++lang) {
    const auto language = static_cast<Language>(lang);
    i18n.setLanguage(language);
    printf("%u %u\n", lang, static_cast<unsigned>(i18n.getLanguage()));
    emit(LANGUAGE_CODES[lang]);
    emit(i18n.getLanguageName(language));
    emit(I18n::getCharacterSet(language));
    printf("%u\n", static_cast<unsigned>(I18n::languageFromCode(LANGUAGE_CODES[lang])));
    for (unsigned id = 0; id < static_cast<unsigned>(StrId::_COUNT); ++id) {
      emit(i18n.get(static_cast<StrId>(id)));
    }
    emit(i18n.get(StrId::_COUNT));
    emit(i18n.get(static_cast<StrId>(65535)));
    i18n.setLanguage(static_cast<Language>(255));
    printf("%u\n", static_cast<unsigned>(i18n.getLanguage()));
  }
  for (unsigned lang = getLanguageCount(); lang < 256; ++lang) {
    emit(i18n.getLanguageName(static_cast<Language>(lang)));
    emit(I18n::getCharacterSet(static_cast<Language>(lang)));
  }
  for (const char* code : {"", "en", "EN ", "INVALID", "\xc3\xa9"}) {
    printf("%u\n", static_cast<unsigned>(I18n::languageFromCode(code)));
  }
  for (auto index : SORTED_LANGUAGE_INDICES) printf("%u ", index);
  putchar('\n');
  for (auto language : V1_LANGUAGES) printf("%u ", static_cast<unsigned>(language));
  putchar('\n');
}
