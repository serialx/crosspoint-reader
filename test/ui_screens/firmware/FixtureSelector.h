#pragma once

#include <string_view>

enum class UiOracleCase {
  Invalid,
  DetailsPlain,
  DetailsHtml,
  DetailsCover,
  FrontlightMono,
  FrontlightWarm,
  SettingsHomeButton,
  KOReaderSync,
  KOReaderAuth
};

constexpr size_t UI_ORACLE_SELECTOR_MAX = 31;

inline UiOracleCase uiOracleCase(const std::string_view name) {
  if (name.empty() || name.size() > UI_ORACLE_SELECTOR_MAX) return UiOracleCase::Invalid;
  static constexpr struct {
    std::string_view name;
    UiOracleCase value;
  } cases[] = {{"details-plain", UiOracleCase::DetailsPlain},
               {"details-html", UiOracleCase::DetailsHtml},
               {"details-cover", UiOracleCase::DetailsCover},
               {"frontlight-mono", UiOracleCase::FrontlightMono},
               {"frontlight-warm", UiOracleCase::FrontlightWarm}};
  for (const auto& entry : cases) {
    if (name == entry.name) return entry.value;
  }
  // Settings-family fixtures.
  if (name == "settings-home-button") return UiOracleCase::SettingsHomeButton;
  if (name == "kosync-hash") return UiOracleCase::KOReaderSync;
  if (name == "kosync-fetch") return UiOracleCase::KOReaderSync;
  if (name == "kosync-clippings") return UiOracleCase::KOReaderSync;
  if (name == "kosync-uploading") return UiOracleCase::KOReaderSync;
  if (name == "kosync-uploaded") return UiOracleCase::KOReaderSync;
  if (name == "kosync-synced") return UiOracleCase::KOReaderSync;
  if (name == "kosync-missing") return UiOracleCase::KOReaderSync;
  if (name == "kosync-credentials") return UiOracleCase::KOReaderSync;
  if (name == "kosync-failed") return UiOracleCase::KOReaderSync;
  if (name == "kosync-result") return UiOracleCase::KOReaderSync;
  if (name == "koauth-busy") return UiOracleCase::KOReaderAuth;
  if (name == "koauth-success") return UiOracleCase::KOReaderAuth;
  if (name == "koauth-failed") return UiOracleCase::KOReaderAuth;
  if (name == "koauth-signup-busy") return UiOracleCase::KOReaderAuth;
  if (name == "koauth-signup-success") return UiOracleCase::KOReaderAuth;
  if (name == "koauth-signup-failed") return UiOracleCase::KOReaderAuth;
  return UiOracleCase::Invalid;
}
