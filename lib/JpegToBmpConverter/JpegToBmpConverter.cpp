#include "JpegToBmpConverter.h"

#include <HalDisplay.h>
#include <HalStorage.h>
#include <JPEGDEC.h>
#include <Logging.h>
#include <Memory.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <xp_ffi.h>
namespace {
constexpr size_t MIN_FREE_HEAP = 52 * 1024;
// Static file pointer for JPEGDEC open callback.
// Safe in single-threaded embedded context; never accessed concurrently.
static HalFile* s_jpegFile = nullptr;
static uint8_t s_jpegIoSinceYield = 0;

static void yieldToIdle() { vTaskDelay(1); }

static void yieldDuringJpegIo() {
  if (++s_jpegIoSinceYield < 4) return;
  s_jpegIoSinceYield = 0;
  yieldToIdle();
}

void* bmpJpegOpen(const char* /*filename*/, int32_t* size) {
  if (!s_jpegFile || !*s_jpegFile) return nullptr;
  s_jpegIoSinceYield = 0;
  s_jpegFile->seek(0);
  *size = static_cast<int32_t>(s_jpegFile->size());
  yieldDuringJpegIo();
  return s_jpegFile;
}

void bmpJpegClose(void* /*handle*/) {
  // Caller owns the file — do not close it here
}

int32_t bmpJpegRead(JPEGFILE* pFile, uint8_t* pBuf, int32_t len) {
  auto* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return 0;
  int32_t n = f->read(pBuf, len);
  if (n < 0) n = 0;
  pFile->iPos += n;
  yieldDuringJpegIo();
  return n;
}

int32_t bmpJpegSeek(JPEGFILE* pFile, int32_t pos) {
  auto* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f || !f->seek(pos)) return -1;
  pFile->iPos = pos;
  yieldDuringJpegIo();
  return pos;
}

enum class JpegDiagnostic : uint32_t {
  Dimensions,
  ProgressiveGrid,
  InvalidDimensions,
  Scaling,
  McuAllocation,
  RowAllocation,
  SmoothAllocation,
  Smoothing,
  ScaleAllocation,
  DitherOneAllocation,
  DitherTwoAllocation,
  BlockOrigin,
  MissingRows
};
struct Context {
  JPEGDEC& jpeg;
  Print& output;
  int sourceWidth, sourceHeight, targetWidth, targetHeight;
  void* state = nullptr;
  XpAssetJpegBlock block = nullptr;
  bool attempted = false;
  int result = 0;
};
int draw(JPEGDRAW* row) {
  auto& self = *static_cast<Context*>(row->pUser);
  return self.block(self.state, row->x, row->y, row->iWidthUsed, row->iHeight, row->iWidth,
                    reinterpret_cast<const uint8_t*>(row->pPixels));
}
void write(void* raw, const uint8_t* bytes, size_t len) {
  auto& out = static_cast<Context*>(raw)->output;
  if (len == 1)
    out.write(*bytes);
  else
    out.write(bytes, len);
}
void progress(void*) { yieldToIdle(); }
bool decode(void* raw, void* state, XpAssetJpegBlock block) {
  auto& self = *static_cast<Context*>(raw);
  self.state = state;
  self.block = block;
  self.attempted = true;
  self.jpeg.setPixelType(EIGHT_BIT_GRAYSCALE);
  self.jpeg.setUserPointer(&self);
  self.result = self.jpeg.decode(0, 0, 0);
  return self.result == 1;
}
// Events describe conversion milestones and failures; formatting stays in C++.
void diagnostic(void* raw, uint32_t event, [[maybe_unused]] int32_t a, [[maybe_unused]] int32_t b,
                [[maybe_unused]] int32_t c, [[maybe_unused]] int32_t d) {
  [[maybe_unused]] const auto& self = *static_cast<Context*>(raw);
  switch (static_cast<JpegDiagnostic>(event)) {
    case JpegDiagnostic::Dimensions:
      LOG_DBG("JPG", "JPEG dimensions: %dx%d", a, b);
      break;
    case JpegDiagnostic::ProgressiveGrid:
      LOG_DBG("JPG", "Progressive JPEG decode uses 1/8 source: %dx%d", a, b);
      break;
    case JpegDiagnostic::InvalidDimensions:
      LOG_DBG("JPG", "Image too large or invalid (%dx%d), max supported: %dx%d", a, b, 2048, 3072);
      break;
    case JpegDiagnostic::Scaling:
      LOG_DBG("JPG", "Scaling source %dx%d (decode grid %dx%d) -> %dx%d (target %dx%d)", self.sourceWidth,
              self.sourceHeight, a, b, c, d, self.targetWidth, self.targetHeight);
      break;
    case JpegDiagnostic::McuAllocation:
      LOG_ERR("JPG", "OOM: MCU buffer (%d bytes)", a);
      break;
    case JpegDiagnostic::RowAllocation:
      LOG_ERR("JPG", "OOM: BMP row buffer");
      break;
    case JpegDiagnostic::SmoothAllocation:
      LOG_ERR("JPG", "OOM: progressive smoothing buffers");
      break;
    case JpegDiagnostic::Smoothing:
      LOG_DBG("JPG", "Progressive smoothing: %dx%d -> %dx%d, buffers=%u bytes", a, b, c, d,
              static_cast<unsigned>(c * 3));
      break;
    case JpegDiagnostic::ScaleAllocation:
      LOG_ERR("JPG", "OOM: scaling buffers");
      break;
    case JpegDiagnostic::DitherOneAllocation:
      LOG_ERR("JPG", "OOM: Atkinson1BitDitherer");
      break;
    case JpegDiagnostic::DitherTwoAllocation:
      LOG_ERR("JPG", "OOM: AtkinsonDitherer");
      break;
    case JpegDiagnostic::BlockOrigin:
      LOG_ERR("JPG", "Unexpected JPEG block origin (%d,%d) for decode grid %dx%d", a, b, c, d);
      break;
    case JpegDiagnostic::MissingRows:
      LOG_ERR("JPG", "No progressive rows decoded for smoothing");
      break;
  }
}
}  // namespace
bool JpegToBmpConverter::jpegFileToBmpStreamInternal(HalFile& file, Print& output, int width, int height, bool oneBit,
                                                     bool crop, bool originalThresholds) {
  LOG_DBG("JPG", "Converting JPEG to %s BMP (target: %dx%d)", oneBit ? "1-bit" : "2-bit", width, height);
  if (ESP.getFreeHeap() < MIN_FREE_HEAP) {
    LOG_ERR("JPG", "Not enough heap for JPEG decoder (%u free, need %u)", ESP.getFreeHeap(), MIN_FREE_HEAP);
    return false;
  }
  s_jpegFile = &file;
  const auto jpeg = makeUniqueNoThrow<JPEGDEC>();
  if (!jpeg) {
    LOG_ERR("JPG", "OOM: JPEG decoder");
    return false;
  }
  if (jpeg->open("", bmpJpegOpen, bmpJpegClose, bmpJpegRead, bmpJpegSeek, draw) != 1) {
    LOG_ERR("JPG", "JPEG open failed (err=%d)", jpeg->getLastError());
    return false;
  }
  const ScopedCleanup cleanup{[&jpeg]() { jpeg->close(); }};
  Context context{*jpeg, output, jpeg->getWidth(), jpeg->getHeight(), width, height};
  const XpAssetJpegServices services{&context, write, progress, diagnostic, decode};
  const XpAssetJpegConfig config{width, height, oneBit, crop, originalThresholds};
  if (!xp_assets_jpeg_bmp(context.sourceWidth, context.sourceHeight, jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE,
                          &config, &services)) {
    if (context.attempted) LOG_ERR("JPG", "JPEG decode failed (rc=%d, err=%d)", context.result, jpeg->getLastError());
    return false;
  }
  LOG_DBG("JPG", "Successfully converted JPEG to BMP");
  return true;
}
// Core function: Convert JPEG file to 2-bit BMP (uses default target size)
bool JpegToBmpConverter::jpegFileToBmpStream(HalFile& jpegFile, Print& bmpOut, bool crop, bool originalThresholds) {
  // Use runtime display dimensions (swapped for portrait cover sizing)
  const int targetWidth = display.getDisplayHeight();
  const int targetHeight = display.getDisplayWidth();
  return jpegFileToBmpStreamInternal(jpegFile, bmpOut, targetWidth, targetHeight, false, crop, originalThresholds);
}

// Convert with custom target size (for thumbnails, 2-bit)
bool JpegToBmpConverter::jpegFileToBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth,
                                                     int targetMaxHeight) {
  return jpegFileToBmpStreamInternal(jpegFile, bmpOut, targetMaxWidth, targetMaxHeight, false);
}

// Convert to 1-bit BMP (black and white only, no grays) for fast home screen rendering
bool JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int targetMaxWidth,
                                                         int targetMaxHeight) {
  return jpegFileToBmpStreamInternal(jpegFile, bmpOut, targetMaxWidth, targetMaxHeight, true, true);
}
