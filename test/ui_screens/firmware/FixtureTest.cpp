#include <cassert>
#include <string_view>

#include "FixtureSelector.h"
#include "UiOracleFrontlight.h"

int main() {
  using namespace std::literals;
  assert(uiOracleCase("details-plain") == UiOracleCase::DetailsPlain);
  assert(uiOracleCase("details-html") == UiOracleCase::DetailsHtml);
  assert(uiOracleCase("details-cover") == UiOracleCase::DetailsCover);
  assert(uiOracleCase("frontlight-mono") == UiOracleCase::FrontlightMono);
  assert(uiOracleCase("frontlight-warm") == UiOracleCase::FrontlightWarm);
  for (auto invalid : {""sv, "unknown"sv, "details-plain\n"sv, "details-plain\0extra"sv,
                       "frontlight-warmXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"sv}) {
    assert(uiOracleCase(invalid) == UiOracleCase::Invalid);
  }
  assert(uiOracleCase("settings-home-button") == UiOracleCase::SettingsHomeButton);
  assert(uiOracleCase("settings-home-button-unknown") == UiOracleCase::Invalid);
  assert(uiOracleCase("opds-browse") == UiOracleCase::OpdsBrowse);
  assert(uiOracleCase("opds-empty") == UiOracleCase::OpdsEmpty);
  assert(uiOracleCase("opds-error") == UiOracleCase::OpdsError);
  assert(uiOracleCase("opds-loading") == UiOracleCase::OpdsLoading);
  assert(uiOracleCase("opds-download") == UiOracleCase::OpdsDownload);
  assert(uiOracleCase("opds-bytes") == UiOracleCase::OpdsBytes);
  assert(uiOracleCase("opds-auth") == UiOracleCase::OpdsAuth);
  assert(uiOracleCase("opds-browse-extra") == UiOracleCase::Invalid);
  assert(uiOracleCase("plugin-browse") == UiOracleCase::PluginBrowse);
  assert(uiOracleCase("plugin-xml") == UiOracleCase::PluginXml);
  assert(uiOracleCase("plugin-lists") == UiOracleCase::PluginLists);
  assert(uiOracleCase("plugin-empty") == UiOracleCase::PluginEmpty);
  assert(uiOracleCase("plugin-error") == UiOracleCase::PluginError);
  assert(uiOracleCase("plugin-loading") == UiOracleCase::PluginLoading);
  assert(uiOracleCase("plugin-wifi") == UiOracleCase::PluginWifi);
  assert(uiOracleCase("plugin-search") == UiOracleCase::PluginSearch);
  assert(uiOracleCase("plugin-download") == UiOracleCase::PluginDownload);
  assert(uiOracleCase("plugin-done") == UiOracleCase::PluginDone);
  assert(uiOracleCase("plugin-no-token") == UiOracleCase::PluginNoToken);
  assert(uiOracleCase("plugin-auth") == UiOracleCase::PluginAuth);
  assert(uiOracleCase("hub-download") == UiOracleCase::HubDownload);
  assert(uiOracleCase("hub-done") == UiOracleCase::HubDone);
  assert(uiOracleCase("hub-error") == UiOracleCase::HubError);
  assert(uiOracleCase("hub-loading") == UiOracleCase::HubLoading);
  UiOracleFrontlight light;
  assert(!light.present());
  light.configure(true);
  assert(light.present() && light.hasColorTemperature());
  assert(light.brightness() == 60 && light.warmth() == 40 && light.isOn());
  light.setBrightness(255);
  light.setWarmth(255);
  light.setOn(false);
  assert(light.brightness() == 100 && light.warmth() == 100 && !light.isOn());
  light.setOn(true);
  assert(light.brightness() == 100 && light.isOn());
  light.configure(false);
  assert(light.present() && !light.hasColorTemperature() && light.warmth() == 0);
}
