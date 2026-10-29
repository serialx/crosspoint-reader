#ifndef CROSSPOINT_UI_ORACLE
#error "UiOracleEntry.cpp is only for the local ui_oracle environment"
#endif

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include "FixtureSelector.h"
#include "UiOracleFrontlight.h"
#include "activities/ActivityManager.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/settings/HomeButtonSettingsActivity.h"
#include "activities/util/DetailsActivity.h"
#include "activities/util/FrontlightPanelActivity.h"

UiOracleFrontlight uiOracleFrontlight;

namespace {
UiOracleCase opdsCase = UiOracleCase::Invalid;
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

bool uiOracleOpdsEnter(OpdsBookBrowserActivity& activity) {
  if (opdsCase == UiOracleCase::Invalid) return false;
  activity.setState(OpdsCatalogActivity::State::BROWSING);
  activity.feedTitle = "Fixture catalog";
  activity.statusMessage = "A synthetic book with a long title that wraps onto a second line";
  activity.errorMessage = "The fixture server could not return its catalog. Please try again.";
  switch (opdsCase) {
    case UiOracleCase::OpdsBrowse:
      activity.entries.reserve(14);
      for (int i = 0; i < 14; ++i) {
        OpdsEntry entry;
        entry.type = i % 3 == 0 ? OpdsEntryType::NAVIGATION : OpdsEntryType::BOOK;
        entry.title = i == 0   ? "All books"
                      : i == 1 ? "A synthetic observatory book with a long title to wrap"
                               : "The Observatory";
        entry.author = "Synthetic Author";
        entry.heading = i == 0 ? "Browse" : i == 1 ? "Publications" : "";
        entry.purchase = i == 2;
        entry.detail = i == 2 ? "$3.00" : "";
        activity.entries.push_back(std::move(entry));
      }
      activity.pageNextHref = "/page/2";
      activity.pageLastHref = "/page/4";
      activity.pageCurrent = 1;
      activity.pageTotal = 4;
      activity.searchTemplate = "/search?q={searchTerms}";
      break;
    case UiOracleCase::OpdsEmpty:
      break;
    case UiOracleCase::OpdsAuth:
      activity.setState(OpdsCatalogActivity::State::AUTH);
      activity.statusMessage = "Passphrase fixture";
      break;
    case UiOracleCase::OpdsError:
      activity.setState(OpdsCatalogActivity::State::ERROR);
      break;
    case UiOracleCase::OpdsLoading:
      activity.setState(OpdsCatalogActivity::State::LOADING);
      activity.statusMessage = "Loading fixture catalog";
      break;
    case UiOracleCase::OpdsDownload:
    case UiOracleCase::OpdsBytes:
      activity.setState(OpdsCatalogActivity::State::DOWNLOADING);
      activity.downloadProgress = 1572864;
      activity.downloadTotal = opdsCase == UiOracleCase::OpdsBytes ? 0 : 4194304;
      break;
    default:
      return false;
  }
  activity.ui.progress = activity.downloadProgress;
  activity.ui.total = activity.downloadTotal;
  activity.requestUpdate();
  return true;
}

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
    case UiOracleCase::OpdsBrowse:
    case UiOracleCase::OpdsEmpty:
    case UiOracleCase::OpdsError:
    case UiOracleCase::OpdsLoading:
    case UiOracleCase::OpdsDownload:
    case UiOracleCase::OpdsAuth:
    case UiOracleCase::OpdsBytes: {
      opdsCase = selected;
      auto activity = makeUniqueNoThrow<OpdsBookBrowserActivity>(renderer, input, OpdsServer{});
      if (!activity) {
        LOG_ERR("ORACLE", "OOM: OPDS activity");
        return;
      }
      activityManager.replaceActivity(std::move(activity));
      return;
    }
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
