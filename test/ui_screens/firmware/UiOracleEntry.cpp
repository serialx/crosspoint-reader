#ifndef CROSSPOINT_UI_ORACLE
#error "UiOracleEntry.cpp is only for the local ui_oracle environment"
#endif

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include "FixtureSelector.h"
#include "UiOracleFrontlight.h"
#include "UiOracleKOReader.h"
#include "activities/ActivityManager.h"
#include "activities/settings/HomeButtonSettingsActivity.h"
#include "activities/util/DetailsActivity.h"
#include "activities/util/FrontlightPanelActivity.h"

UiOracleFrontlight uiOracleFrontlight;

namespace {
constexpr char SELECTOR_PATH[] = "/.crosspoint/ui-oracle-case.txt";

void openDetails(GfxRenderer& renderer, MappedInputManager& input, const UiOracleCase selected) {
  // Static storage keeps DetailsSpec's string objects off the small loop stack.
  static DetailsSpec spec;
  spec.header = "Fixture catalog";
  spec.title = "The Observatory";
  spec.author = "Synthetic Author";
  spec.source = "Local fixture";
  spec.metadata = "Edition 1 | English";
  spec.status = "Available";
  spec.copies = "2 copies";
  spec.holds = "No holds";
  spec.descriptionHeading = "Description";
  spec.descriptionHtml = selected == UiOracleCase::DetailsHtml;
  spec.description = spec.descriptionHtml
                         ? "<h2>A clear night</h2><p>A <b>synthetic</b> description with <i>styled text</i>.</p>"
                           "<p>Explore the observatory and its library.</p>"
                         : "A synthetic description of an observatory and its library. "
                           "Every title and author in this fixture is public test data.";
  spec.showCover = selected != UiOracleCase::DetailsPlain;
  spec.coverPath = selected == UiOracleCase::DetailsCover ? "/OracleCover.png" : "";
  spec.actionLabel = "Read fixture";
  auto activity = makeUniqueNoThrow<DetailsActivity>(renderer, input, std::move(spec));
  if (!activity) {
    LOG_ERR("ORACLE", "OOM: Details activity (%u bytes)", static_cast<unsigned>(sizeof(DetailsActivity)));
    return;
  }
  activityManager.replaceActivity(std::move(activity));
}
}  // namespace

void uiOracleEnter(GfxRenderer& renderer, MappedInputManager& input) {
  if (!Storage.exists(SELECTOR_PATH)) return;
  HalFile file;
  if (!Storage.openFileForRead("ORACLE", SELECTOR_PATH, file)) return;
  const size_t size = file.size();
  if (size == 0 || size > UI_ORACLE_SELECTOR_MAX) {
    LOG_ERR("ORACLE", "Rejected selector length");
    return;
  }
  char selector[UI_ORACLE_SELECTOR_MAX];
  if (file.read(selector, size) != static_cast<int>(size)) {
    LOG_ERR("ORACLE", "Failed to read selector");
    return;
  }
  const auto selected = uiOracleCase(std::string_view(selector, size));
  switch (selected) {
    case UiOracleCase::KOReaderSync:
    case UiOracleCase::KOReaderAuth:
      UiOracleKOReader::enter(renderer, input, std::string_view(selector, size));
      return;
    // Settings-family fixtures bypass only the parent capability gate.
    case UiOracleCase::SettingsHomeButton: {
      auto activity = makeUniqueNoThrow<HomeButtonSettingsActivity>(renderer, input);
      if (!activity) {
        LOG_ERR("ORACLE", "OOM: Home button settings");
        return;
      }
      activityManager.replaceActivity(std::move(activity));
      return;
    }
    case UiOracleCase::Invalid:
      LOG_ERR("ORACLE", "Rejected unknown selector");
      return;
    case UiOracleCase::DetailsPlain:
    case UiOracleCase::DetailsHtml:
    case UiOracleCase::DetailsCover:
      openDetails(renderer, input, selected);
      return;
    case UiOracleCase::FrontlightMono:
    case UiOracleCase::FrontlightWarm: {
      uiOracleFrontlight.configure(selected == UiOracleCase::FrontlightWarm);
      // The sheet preserves the framebuffer below it; finish Home before pushing.
      activityManager.loop();
      const auto started = millis();
      while (!activityManager.isForegroundHomeShown()) {
        if (millis() - started > 10000) {
          LOG_ERR("ORACLE", "Home did not finish painting");
          return;
        }
        delay(1);
      }
      auto activity = makeUniqueNoThrow<FrontlightPanelActivity>(renderer, input);
      if (!activity) {
        LOG_ERR("ORACLE", "OOM: Frontlight activity (%u bytes)",
                static_cast<unsigned>(sizeof(FrontlightPanelActivity)));
        return;
      }
      activityManager.pushActivity(std::move(activity));
      return;
    }
  }
}
