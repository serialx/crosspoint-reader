#ifndef CROSSPOINT_UI_ORACLE
#error "Update fixtures are only for the local ui_oracle environment"
#endif

#include <I18n.h>
#include <Memory.h>

#include <cstdlib>
#include <cstring>
#include <string_view>

#include "activities/ActivityManager.h"
#include "activities/settings/ClearCacheActivity.h"
#include "activities/settings/FontDownloadActivity.h"
#include "activities/settings/OtaUpdateActivity.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"

// This TU is built with -fno-access-control only in the local oracle environment.
// Render the real activity, but never enter its network/flash/deletion lifecycle.
template <class T>
class UpdateFixture final : public Activity {
 public:
  T screen;
  UpdateFixture(const char* name, GfxRenderer& renderer, MappedInputManager& input)
      : Activity(name, renderer, input), screen(renderer, input) {}
  void onEnter() override {
    Activity::onEnter();
    requestUpdate();
  }
  void render(RenderLock&& lock) override { screen.render(std::move(lock)); }
  void loop() override {}
};

bool uiUpdateOracleEnter(GfxRenderer& renderer, MappedInputManager& input, std::string_view selector) {
  if (!selector.starts_with("upd-")) return false;
  const auto last = selector.find_last_of('-');
  if (last == std::string_view::npos || last + 2 != selector.size()) return false;
  const int phase = selector.back() - '0';
  if (phase < 0 || phase > 7) return false;
  const auto kind = selector.substr(4, last - 4);
  if (kind == "ota") {
    auto fixture = makeUniqueNoThrow<UpdateFixture<OtaUpdateActivity>>("OtaUpdate", renderer, input);
    if (!fixture) return true;
    auto& screen = fixture->screen;
    screen.model.phase = phase;
    screen.model.processed = 49;
    screen.model.total = 100;
    screen.updater.latestVersion = "9.9.9";
    if (phase == 2) {
      screen.model.popup_active = 1;
      screen.model.popup_selected = 1;
    }
    if (phase == 5) screen.failedDetail = tr(STR_FIRMWARE_WRONG_DEVICE);
    activityManager.replaceActivity(std::move(fixture));
    return true;
  }
  if (kind == "sd") {
    auto fixture = makeUniqueNoThrow<UpdateFixture<SdFirmwareUpdateActivity>>("SdFirmwareUpdate", renderer, input);
    if (!fixture) return true;
    auto& screen = fixture->screen;
    screen.model.phase = phase;
    screen.model.recovery = phase == 0;
    screen.firmwareSize = 100;
    screen.model.processed = 49;
    screen.model.total = 100;
    screen.errorMessage = tr(STR_FIRMWARE_WRONG_DEVICE);
    activityManager.replaceActivity(std::move(fixture));
    return true;
  }
  if (kind == "cache") {
    auto fixture = makeUniqueNoThrow<UpdateFixture<ClearCacheActivity>>("ClearCache", renderer, input);
    if (!fixture) return true;
    auto& screen = fixture->screen;
    screen.model.phase = phase;
    screen.clearedCount = 5;
    screen.failedCount = 2;
    activityManager.replaceActivity(std::move(fixture));
    return true;
  }
  if (kind == "font") {
    auto fixture = makeUniqueNoThrow<UpdateFixture<FontDownloadActivity>>("FontDownload", renderer, input);
    if (!fixture) return true;
    auto& screen = fixture->screen;
    constexpr char strings[] = "\0Fixture family\0A synthetic font family description\0Latin\0";
    screen.stringArena_ = makeUniqueNoThrow<char[]>(sizeof(strings));
    if (!screen.stringArena_) return true;
    memcpy(screen.stringArena_.get(), strings, sizeof(strings));
    screen.families_ = {{1, 16, 0, 2, 2048, 1, false, false}, {1, 16, 0, 2, 2048, 1, true, false}};
    screen.scriptGroupLabels_ = {52};
    screen.filteredIndices_ = {0, 1};
    screen.model.phase = phase;
    screen.model.has_groups = 1;
    screen.model.file_count = 2;
    screen.model.processed = 49;
    screen.model.total = 100;
    screen.errorMessage_ = "Fixture transfer error";
    screen.UpdateUiActivity::onEnter();
    screen.requestUpdate();
    activityManager.replaceActivity(std::move(fixture));
    return true;
  }
  return false;
}
