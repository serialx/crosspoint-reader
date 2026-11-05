#pragma once
#include <HalStorage.h>
struct PngToBmpConverter {
  static bool pngFileTo1BitBmpStreamWithSize(HalFile&, HalFile&, int width, int height) {
    return booktest::step("png-thumb", std::to_string(width), height);
  }
  static bool pngFileToBmpStream(HalFile&, HalFile&, bool crop, bool thresholds) {
    return booktest::step("png-cover", std::to_string(crop), thresholds);
  }
};
