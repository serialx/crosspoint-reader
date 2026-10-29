#pragma once
#include <BookCodec.h>
struct GifToBmpConverter {
  static bool gifFileToBmpStream(HalFile&, Print& out, bool cropped = false, bool original = false) {
    return bookCodec(out, 3, false, 0, 0, cropped, original);
  }
  static bool gifFileTo1BitBmpStreamWithSize(HalFile&, Print& out, int width, int height) {
    return bookCodec(out, 3, true, width, height, false, false);
  }
};
