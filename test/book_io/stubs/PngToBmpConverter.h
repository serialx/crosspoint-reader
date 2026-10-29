#pragma once
#include <HalStorage.h>
struct PngToBmpConverter {
  static bool pngFileToBmpStream(HalFile&, Print&, bool = false, bool = false) { return false; }
  static bool pngFileTo1BitBmpStreamWithSize(HalFile&, Print&, int, int) { return false; }
};
