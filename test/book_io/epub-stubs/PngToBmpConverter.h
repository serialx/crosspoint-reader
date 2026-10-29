#pragma once
#include <BookCodec.h>
struct PngToBmpConverter {
  static bool pngFileToBmpStream(HalFile&, Print& out, bool cropped = false, bool original = false) {
    return bookCodec(out, 2, false, 0, 0, cropped, original);
  }
  static bool pngFileTo1BitBmpStreamWithSize(HalFile&, Print& out, int width, int height) {
    return bookCodec(out, 2, true, width, height, false, false);
  }
};
