#pragma once
enum class StrId { STR_FONT_PREVIEW_TEXT };
struct I18nStub {
  const char* sample = "The quick brown fox jumps over the lazy dog.";
  const char* get(StrId) const { return sample; }
};
inline I18nStub I18N;
