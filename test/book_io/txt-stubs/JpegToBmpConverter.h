#pragma once
#include <HalStorage.h>
struct JpegToBmpConverter {
  static bool jpegFileTo1BitBmpStreamWithSize(HalFile&, HalFile&, int width, int height) {
    return booktest::step("jpeg-thumb", std::to_string(width), height);
  }
  static bool jpegFileToBmpStream(HalFile&, HalFile&, bool crop, bool thresholds) {
    return booktest::step("jpeg-cover", std::to_string(crop), thresholds);
  }
};
