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
  OpdsBrowse,
  OpdsEmpty,
  OpdsError,
  OpdsLoading,
  OpdsDownload,
  OpdsBytes,
  OpdsAuth
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
  if (name == "opds-browse") return UiOracleCase::OpdsBrowse;
  if (name == "opds-empty") return UiOracleCase::OpdsEmpty;
  if (name == "opds-error") return UiOracleCase::OpdsError;
  if (name == "opds-loading") return UiOracleCase::OpdsLoading;
  if (name == "opds-download") return UiOracleCase::OpdsDownload;
  if (name == "opds-bytes") return UiOracleCase::OpdsBytes;
  if (name == "opds-auth") return UiOracleCase::OpdsAuth;
  // Settings-family fixtures.
  if (name == "settings-home-button") return UiOracleCase::SettingsHomeButton;
  return UiOracleCase::Invalid;
}
