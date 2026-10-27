#include "PngToBmpConverter.h"

#include <BuildScratch.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Memory.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <xp_ffi.h>

namespace {
struct Context {
  HalFile& input;
  Print& output;
};
size_t read(void* context, uint32_t offset, uint8_t* bytes, size_t length) {
  auto& file = static_cast<Context*>(context)->input;
  if (file.position() != offset && !file.seek(offset)) return 0;
  const int count = file.read(bytes, length);
  return count > 0 ? static_cast<size_t>(count) : 0;
}
bool write(void* context, uint32_t, const uint8_t* bytes, size_t length) {
  return static_cast<Context*>(context)->output.write(bytes, length) == length;
}
void progress(void*) { vTaskDelay(1); }
}  // namespace

bool PngToBmpConverter::pngFileToBmpStreamInternal(HalFile& pngFile, Print& bmpOut, int targetWidth, int targetHeight,
                                                   bool oneBit, bool crop, bool originalThresholds) {
  if (pngFile.size() > UINT32_MAX) return false;
  Context context{pngFile, bmpOut};
  const XpPngServices services{&context, read, nullptr, write, progress, static_cast<uint32_t>(pngFile.size())};
  const XpPngBmpConfig config{targetWidth, targetHeight, oneBit, crop, originalThresholds};
  const size_t scratchBytes = xp_png_scratch_size();
  auto* scratch = buildscratch::claim(scratchBytes);
  const ScopedCleanup cleanup{[scratch]() { buildscratch::release(scratch); }};
  if (!xp_png_bmp(&services, &config, scratch, scratch ? scratchBytes : 0)) {
    LOG_ERR("PNG", "PNG to BMP conversion failed");
    return false;
  }
  return true;
}

bool PngToBmpConverter::pngFileToBmpStream(HalFile& pngFile, Print& bmpOut, bool crop, bool originalThresholds) {
  return pngFileToBmpStreamInternal(pngFile, bmpOut, display.getDisplayHeight(), display.getDisplayWidth(), false, crop,
                                    originalThresholds);
}

bool PngToBmpConverter::pngFileToBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth,
                                                   int targetMaxHeight) {
  return pngFileToBmpStreamInternal(pngFile, bmpOut, targetMaxWidth, targetMaxHeight, false);
}

bool PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth,
                                                       int targetMaxHeight) {
  return pngFileToBmpStreamInternal(pngFile, bmpOut, targetMaxWidth, targetMaxHeight, true, true);
}
