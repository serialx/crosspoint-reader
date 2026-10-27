#include "GifToBmpConverter.h"

#include <GifCommon.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Print.h>

namespace {
bool writeBmp(void* context, const uint8_t* bytes, size_t len) {
  return static_cast<Print*>(context)->write(bytes, len) == len;
}
}  // namespace

bool GifToBmpConverter::gifFileToBmpStreamInternal(HalFile& file, Print& output, int width, int height, bool oneBit,
                                                   bool crop, bool originalThresholds) {
  GifCommon::Source source{file};
  const auto input = source.input();
  const XpGifBmpConfig config{width, height, oneBit, crop, originalThresholds};
  const XpGifOutput sink{&output, writeBmp};
  if (xp_gif_bmp(&input, &config, &sink)) return true;
  LOG_ERR("GIF", "GIF BMP conversion failed (input, allocation, or output)");
  return false;
}

bool GifToBmpConverter::gifFileToBmpStream(HalFile& file, Print& output, bool crop, bool originalThresholds) {
  return gifFileToBmpStreamInternal(file, output, display.getDisplayHeight(), display.getDisplayWidth(), false, crop,
                                    originalThresholds);
}

bool GifToBmpConverter::gifFileTo1BitBmpStreamWithSize(HalFile& file, Print& output, int width, int height) {
  return gifFileToBmpStreamInternal(file, output, width, height, true, false);
}
