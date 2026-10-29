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
