#pragma once
#include "BootUiActivity.h"

class BootActivity final : public BootUiActivity {
 public:
  explicit BootActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : BootUiActivity("Boot", renderer, mappedInput) {}
  void onEnter() override;
};
