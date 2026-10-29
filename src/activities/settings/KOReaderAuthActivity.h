#pragma once
#include "activities/reader/KOReaderUiActivity.h"

class KOReaderAuthActivity final : public KOReaderUiActivity {
 public:
  enum class Mode { AUTHENTICATE, SIGN_UP };
  explicit KOReaderAuthActivity(GfxRenderer& renderer, MappedInputManager& input, Mode mode = Mode::AUTHENTICATE)
      : KOReaderUiActivity("KOReaderAuth", renderer, input, mode == Mode::SIGN_UP ? XP_KO_SIGNUP : XP_KO_AUTH) {}
  void onEnter() override;
  void onExit() override;

 private:
#ifdef CROSSPOINT_UI_ORACLE
  friend struct UiOracleKOReader;
  bool oracleFreeze = false;
#endif
  void onWifiSelectionComplete(bool);
  void performAuthentication();
  void performEffect(const XpUiEffect&) override;
  static uint32_t service(void*, uint32_t, uint32_t, uint32_t);
};
