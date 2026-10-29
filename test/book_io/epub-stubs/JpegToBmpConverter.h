#pragma once
#include <BookCodec.h>
struct JpegToBmpConverter {
  static bool jpegFileToBmpStream(HalFile&, Print& out, bool cropped = false, bool original = false) {
    return bookCodec(out, 1, false, 0, 0, cropped, original);
  }
  static bool jpegFileTo1BitBmpStreamWithSize(HalFile&, Print& out, int width, int height) {
    return bookCodec(out, 1, true, width, height, false, false);
  }
};
