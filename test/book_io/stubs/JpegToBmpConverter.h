#pragma once
#include <HalStorage.h>
struct JpegToBmpConverter {
  static bool jpegFileToBmpStream(HalFile&, Print&, bool = false, bool = false) { return false; }
  static bool jpegFileTo1BitBmpStreamWithSize(HalFile&, Print&, int, int) { return false; }
};
