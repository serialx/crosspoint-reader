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
      auto auth = makeUniqueNoThrow<KOReaderAuthActivity>(
          renderer, input, signup ? KOReaderAuthActivity::Mode::SIGN_UP : KOReaderAuthActivity::Mode::AUTHENTICATE);
      if (!auth) return;
      auth->oracleFreeze = true;
      auth->publish(name.ends_with("-success")  ? XP_KO_AUTHENTICATED
                    : name.ends_with("-failed") ? XP_KO_FAILED
                                                : XP_KO_AUTHENTICATING,
                    name.ends_with("-failed") ? XP_KO_WIFI_FAILED : XP_KO_AUTH_BUSY);
      activityManager.replaceActivity(std::move(auth));
      return;
    }
    auto sync = makeUniqueNoThrow<KOReaderSyncActivity>(renderer, input, "/Oracle.epub", CrossPointPosition{0, 2, 10},
                                                        SavedProgressPosition{"/body/DocFragment[1]/body/p[3]", 0.25f},
                                                        "First chapter");
    if (!sync) return;
    sync->oracleFreeze = true;
    sync->publish(XP_KO_SYNCING, XP_KO_HASH);
    if (name == "kosync-fetch") sync->publish(XP_KO_SYNCING, XP_KO_FETCH);
    if (name == "kosync-clippings") sync->publish(XP_KO_SYNCING, XP_KO_CLIPPINGS);
    if (name == "kosync-uploading") sync->publish(XP_KO_UPLOADING, XP_KO_UPLOAD);
    if (name == "kosync-uploaded") sync->publish(XP_KO_UPLOADED);
    if (name == "kosync-synced") sync->publish(XP_KO_SYNCHRONIZED);
    if (name == "kosync-missing") sync->publish(XP_KO_MISSING);
    if (name == "kosync-credentials") sync->publish(XP_KO_NO_CREDENTIALS);
    if (name == "kosync-failed") sync->publish(XP_KO_FAILED, XP_KO_HASH_FAILED);
    if (name == "kosync-result") {
      sync->publish(XP_KO_RESULT);
      sync->remoteProgress.percentage = 0.75f;
      sync->remoteProgress.device = "Oracle reader";
      sync->remotePosition = {1, 3, 10};
      sync->epub = std::make_shared<Epub>("/Oracle.epub", "/.crosspoint");
      sync->epub->setupCacheDir();
      if (!sync->epub->load(true, true)) return;
    }
    activityManager.replaceActivity(std::move(sync));
  }
};
