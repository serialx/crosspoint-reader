#pragma once

#include <functional>
#include <string>

#include "MappedInputManager.h"
#include "activities/boot_sleep/BootUiActivity.h"

class BmpViewerActivity final : public BootUiActivity {
 public:
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override { RustActivityAdapter::loop(); }
  XpUiInterest rustInterest(uint32_t) const override;
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;
  void drawBootHints() override;

 private:
  void loadSiblingImages();
  void doSetSleepCover();
  bool canSetSleepCover() const;
  bool renderImage();
  bool saveJpegSleepCover();

  std::string filePath;
  std::vector<std::string> siblingImages;
  XpUiImageState state{-1, 0, 0, {}};
};
