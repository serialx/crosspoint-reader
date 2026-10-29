#pragma once
#include <HalStorage.h>
struct GifToBmpConverter {
  static bool gifFileToBmpStream(HalFile&, Print&, bool = false, bool = false) { return false; }
  static bool gifFileTo1BitBmpStreamWithSize(HalFile&, Print&, int, int) { return false; }
};
