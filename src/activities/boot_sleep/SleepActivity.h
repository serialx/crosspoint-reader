#pragma once
#include <string>

#include "BootUiActivity.h"

class Bitmap;
class HalFile;

class SleepActivity final : public BootUiActivity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : BootUiActivity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

 private:
  XpUiSleepPlan sleepPlan{};
  void renderSleepRoute(uint8_t route);
  void renderDefaultSleepScreen();
  void renderCustomSleepScreen();
  void renderCoverSleepScreen();
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool preserveBackground = false);
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog);
  bool renderTransparentOverlayPng(const std::string& path);
  bool renderSleepOverlayPath(const std::string& path);
  void renderLastScreenSleepScreen();
  void renderTransparentCustomSleepScreen();
  void renderBlankSleepScreen();

  bool fromTimeout = false;
  // Cover + Transparent: the cover under the overlay, and the same cover while its gray planes are redrawn.
  mutable const Bitmap* overlayCover = nullptr;
  mutable const Bitmap* overlayGrayCover = nullptr;
};
