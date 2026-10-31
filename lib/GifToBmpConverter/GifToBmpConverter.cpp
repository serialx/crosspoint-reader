#include "GifToBmpConverter.h"

#include <GifCommon.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Print.h>

namespace {
struct Output {
  Print& stream;
  bool header = true;
};
bool writeBmp(void* context, const uint8_t* bytes, size_t len) {
  auto& self = *static_cast<Output*>(context);
  const size_t written = self.stream.write(bytes, len);
  if (!self.header && written != len)
    LOG_ERR("GIF", "Short BMP row write (%u of %u bytes)", static_cast<unsigned>(written), static_cast<unsigned>(len));
  self.header = false;
  return written == len;
}
}  // namespace

bool GifToBmpConverter::gifFileToBmpStreamInternal(HalFile& file, Print& output, int width, int height, bool oneBit,
                                                   bool crop, bool originalThresholds) {
  LOG_DBG("GIF", "Converting GIF to %s BMP (target: %dx%d)", oneBit ? "1-bit" : "2-bit", width, height);
  GifCommon::Source source{file};
  source.bmp = true;
  const auto input = source.input();
  const XpGifBmpConfig config{width, height, oneBit, crop, originalThresholds};
  Output stream{output};
  const XpGifOutput sink{&stream, writeBmp};
  if (xp_gif_bmp(&input, &config, &sink)) return true;
  return false;
}

bool GifToBmpConverter::gifFileToBmpStream(HalFile& file, Print& output, bool crop, bool originalThresholds) {
  return gifFileToBmpStreamInternal(file, output, display.getDisplayHeight(), display.getDisplayWidth(), false, crop,
                                    originalThresholds);
}

bool GifToBmpConverter::gifFileTo1BitBmpStreamWithSize(HalFile& file, Print& output, int width, int height) {
  return gifFileToBmpStreamInternal(file, output, width, height, true, false);
}
