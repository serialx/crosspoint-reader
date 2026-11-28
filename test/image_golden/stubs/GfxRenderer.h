#pragma once
#include <Arduino.h>

#include <cstdint>
#include <vector>

class GfxRenderer {
 public:
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  RenderMode mode = BW;
  Orientation orientation = Portrait;
  bool absolute = false;
  int origin = 0, rows = 528;
  std::vector<uint8_t> buffer = std::vector<uint8_t>(792 * 528 / 8, 0xa5);
  mutable std::vector<uint8_t> calls;
  uint8_t* getWriteTarget() {
    calls.push_back(0);
    return buffer.data();
  }
  int getWriteOriginY() const {
    calls.push_back(1);
    return origin;
  }
  int getWriteRows() const {
    calls.push_back(2);
    return rows;
  }
  RenderMode getRenderMode() const {
    calls.push_back(3);
    return mode;
  }
  bool grayPlanesAreAbsolute() const {
    calls.push_back(4);
    return absolute;
  }
  int getDisplayWidthBytes() const {
    calls.push_back(5);
    return 99;
  }
  int getDisplayWidth() const {
    calls.push_back(6);
    return 792;
  }
  int getDisplayHeight() const {
    calls.push_back(7);
    return 528;
  }
  Orientation getOrientation() const {
    calls.push_back(8);
    return orientation;
  }
  int getScreenWidth() const { return orientation % 2 == 0 ? 528 : 792; }
  int getScreenHeight() const { return orientation % 2 == 0 ? 792 : 528; }
};
