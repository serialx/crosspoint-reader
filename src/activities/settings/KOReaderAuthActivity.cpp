#include "KOReaderAuthActivity.h"

#include <I18n.h>
#include <WiFi.h>

#include "KOReaderSyncClient.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "network/WifiPowerSaveGuard.h"

void KOReaderAuthActivity::onWifiSelectionComplete(bool success) {
  if (!success) {
    publish(XP_KO_FAILED, XP_KO_WIFI_FAILED);
    requestUpdate();
    return;
  }
  publish(XP_KO_AUTHENTICATING, XP_KO_AUTH_BUSY);
  requestUpdate();
  performAuthentication();
}
void KOReaderAuthActivity::performAuthentication() {
  RunGuard guard(runActive);
  if (!guard) return;
  WifiPowerSaveGuard psGuard;
  const XpKoServices services{this, &KOReaderAuthActivity::service};
  xp_ui_koreader_auth(model.kind == XP_KO_SIGNUP, &services);
}
void KOReaderAuthActivity::onEnter() {
  Activity::onEnter();
  if (!initializeUi()) {
    finish();
    return;
  }
#ifdef CROSSPOINT_UI_ORACLE
  if (oracleFreeze) {
    requestUpdate();
    return;
  }
#endif
  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelectionComplete(true);
    return;
  }
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}
void KOReaderAuthActivity::onExit() {
  KOReaderUiActivity::onExit();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}
void KOReaderAuthActivity::performEffect(const XpUiEffect& effect) {
  if (effect.kind == XP_UI_ACTIVATE) finish();
}
uint32_t KOReaderAuthActivity::service(void* context, uint32_t op, uint32_t a, uint32_t b) {
  auto& self = *static_cast<KOReaderAuthActivity*>(context);
  switch (static_cast<XpKoOperation>(op)) {
    case XP_KO_AUTH_REQUEST:
      return a ? KOReaderSyncClient::createUser() : KOReaderSyncClient::authenticate();
    case XP_KO_PUBLISH:
      self.publish(static_cast<XpKoPhase>(a), static_cast<XpKoMessage>(b & 255), 0,
                   KOReaderSyncClient::errorString(static_cast<KOReaderSyncClient::Error>((b >> 8) & 255)));
      return 0;
    case XP_KO_REPAINT:
      self.requestUpdate();
      return 0;
    default:
      return 0;  // The authentication run only binds these three operations.
  }
}
