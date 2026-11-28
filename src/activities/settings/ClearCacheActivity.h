#pragma once
#include "UpdateUiActivity.h"
class ClearCacheActivity final : public UpdateUiActivity {
 public:
  ClearCacheActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UpdateUiActivity("ClearCache", renderer, input, XP_UI_UPDATE_CACHE) {}
  void onEnter() override;

 private:
  int clearedCount = 0, failedCount = 0;
  std::string resultText;
  void beginClear();
  void clearCache();
  void performAction(XpUiUpdateAction, int32_t) override;
  void prepareText() override;
  const char* dynamicText(uint32_t) const override;
};
