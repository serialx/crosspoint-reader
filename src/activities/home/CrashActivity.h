#pragma once
#include "activities/boot_sleep/BootUiActivity.h"

class CrashActivity final : public BootUiActivity {
  std::string panicMessage;

 public:
  explicit CrashActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : BootUiActivity("Crash", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
