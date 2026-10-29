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
  OpdsAuth,
  PluginBrowse,
  PluginXml,
  PluginLists,
  PluginEmpty,
  PluginError,
  PluginLoading,
  PluginWifi,
  PluginSearch,
  PluginDownload,
  PluginDone,
  PluginNoToken,
  PluginAuth,
  HubDownload,
  HubDone,
  HubError,
  HubLoading,
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
  if (name == "plugin-browse") return UiOracleCase::PluginBrowse;
  if (name == "plugin-xml") return UiOracleCase::PluginXml;
  if (name == "plugin-lists") return UiOracleCase::PluginLists;
  if (name == "plugin-empty") return UiOracleCase::PluginEmpty;
  if (name == "plugin-error") return UiOracleCase::PluginError;
  if (name == "plugin-loading") return UiOracleCase::PluginLoading;
  if (name == "plugin-wifi") return UiOracleCase::PluginWifi;
  if (name == "plugin-search") return UiOracleCase::PluginSearch;
  if (name == "plugin-download") return UiOracleCase::PluginDownload;
  if (name == "plugin-done") return UiOracleCase::PluginDone;
  if (name == "plugin-no-token") return UiOracleCase::PluginNoToken;
  if (name == "plugin-auth") return UiOracleCase::PluginAuth;
  if (name == "hub-download") return UiOracleCase::HubDownload;
  if (name == "hub-done") return UiOracleCase::HubDone;
  if (name == "hub-error") return UiOracleCase::HubError;
  if (name == "hub-loading") return UiOracleCase::HubLoading;
  // Settings-family fixtures.
  if (name == "settings-home-button") return UiOracleCase::SettingsHomeButton;
  return UiOracleCase::Invalid;
}
