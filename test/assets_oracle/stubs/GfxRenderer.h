#pragma once
#include <Arduino.h>
#include <FontCacheManager.h>
#include <HalDisplay.h>
#include <HalStorage.h>

#include <cstdint>
#include <vector>
class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  RenderMode mode = BW;
  Orientation orientation = Portrait;
  bool absolute = false;
  FontCacheManager fonts;
  std::vector<uint8_t> buffer = std::vector<uint8_t>(792 * 528 / 8, 0xa5);
  mutable std::vector<int> rectangles;
  uint8_t* getWriteTarget() { return buffer.data(); }
  int getWriteOriginY() const { return 0; }
  int getWriteRows() const { return 528; }
  RenderMode getRenderMode() const { return mode; }
  bool grayPlanesAreAbsolute() const { return absolute; }
  int getDisplayWidthBytes() const { return 99; }
  Orientation getOrientation() const { return orientation; }
  FontCacheManager* getFontCacheManager() { return &fonts; }
  bool glyphIntersectsStrip(int, int, int, int) const { return true; }
  void preserveImagePolarity(int, int, int, int) {}
  const uint8_t* getFrameBuffer() const { return nullptr; }
  int getDisplayWidth() const { return 792; }
  int getDisplayHeight() const { return 528; }
  int getScreenWidth() const { return 528; }
  int getScreenHeight() const { return 792; }
  bool storeBwBuffer() { return false; }
  void restoreBwBuffer() {}
  void getOrientedViewableTRBL(int* a, int* b, int* c, int* d) { *a = *b = *c = *d = 0; }
  void drawRect(int, int, int, int, int, bool) {}
  void fillRect(int x, int y, int w, int h, bool b) const { rectangles.insert(rectangles.end(), {x, y, w, h, int(b)}); }
  void displayBuffer(HalDisplay::RefreshMode = HalDisplay::RefreshMode::HALF_REFRESH) {}
};
