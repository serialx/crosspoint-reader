#pragma once
#ifndef CROSSPOINT_UI_ORACLE
#error "KOReader fixture entry is only for ui_oracle"
#endif

#include <I18n.h>
#include <Memory.h>

#include "activities/reader/KOReaderSyncActivity.h"
#include "activities/settings/KOReaderAuthActivity.h"

struct UiOracleKOReader {
  static void enter(GfxRenderer& renderer, MappedInputManager& input, std::string_view name) {
    if (name.starts_with("koauth-")) {
      const bool signup = name.starts_with("koauth-signup-");
      auto auth = makeUniqueNoThrow<KOReaderAuthActivity>(renderer, input,
          signup ? KOReaderAuthActivity::Mode::SIGN_UP : KOReaderAuthActivity::Mode::AUTHENTICATE);
      if (!auth) return;
      auth->oracleFreeze = true;
      auth->state = name.ends_with("-success") ? KOReaderAuthActivity::SUCCESS
                    : name.ends_with("-failed") ? KOReaderAuthActivity::FAILED
                    : KOReaderAuthActivity::AUTHENTICATING;
      auth->statusMessage = signup ? tr(STR_CREATING_ACCOUNT) : tr(STR_AUTHENTICATING);
      auth->errorMessage = tr(STR_WIFI_CONN_FAILED);
      activityManager.replaceActivity(std::move(auth));
      return;
    }
    auto sync = makeUniqueNoThrow<KOReaderSyncActivity>(renderer, input, "/Oracle.epub",
        CrossPointPosition{0, 2, 10}, SavedProgressPosition{"/body/DocFragment[1]/body/p[3]", 0.25f}, "First chapter");
    if (!sync) return;
    sync->oracleFreeze = true;
    sync->state = KOReaderSyncActivity::SYNCING;
    sync->statusMessage = tr(STR_CALC_HASH);
    if (name == "kosync-fetch") sync->statusMessage = tr(STR_FETCH_PROGRESS);
    if (name == "kosync-clippings") sync->statusMessage = tr(STR_SYNCING_CLIPPINGS);
    if (name == "kosync-uploading") {
      sync->state = KOReaderSyncActivity::UPLOADING;
      sync->statusMessage = tr(STR_UPLOAD_PROGRESS);
    }
    if (name == "kosync-uploaded") sync->state = KOReaderSyncActivity::UPLOAD_COMPLETE;
    if (name == "kosync-synced") sync->state = KOReaderSyncActivity::SYNC_COMPLETE;
    if (name == "kosync-missing") sync->state = KOReaderSyncActivity::NO_REMOTE_PROGRESS;
    if (name == "kosync-credentials") sync->state = KOReaderSyncActivity::NO_CREDENTIALS;
    if (name == "kosync-failed") {
      sync->state = KOReaderSyncActivity::SYNC_FAILED;
      sync->statusMessage = tr(STR_HASH_FAILED);
    }
    if (name == "kosync-result") {
      sync->state = KOReaderSyncActivity::SHOWING_RESULT;
      sync->remoteProgress.percentage = 0.75f;
      sync->remoteProgress.device = "Oracle reader";
      sync->remotePosition = {1, 3, 10};
      sync->epub = std::make_shared<Epub>("/Oracle.epub", "/.crosspoint");
      sync->epub->setupCacheDir();
      if (!sync->epub->load(true, true)) return;
    }
    sync->resetUi();
    sync->app.on(1, &KOReaderSyncActivity::onResultRow, sync.get());
    sync->app.setScreen(&KOReaderSyncActivity::resultScreen, sync.get());
    activityManager.replaceActivity(std::move(sync));
  }
};
