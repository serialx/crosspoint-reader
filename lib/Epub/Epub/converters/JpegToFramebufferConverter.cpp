#include "JpegToFramebufferConverter.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <JPEGDEC.h>
#include <Logging.h>
#include <Memory.h>

#include <memory>
#include <new>

#include "ImagePipeline.h"

namespace {

// Context struct passed through JPEGDEC callbacks to avoid global mutable state.
// The draw callback receives this via pDraw->pUser (set by setUserPointer()).
// The file I/O callbacks receive the HalFile* via pFile->fHandle (set by jpegOpen()).
struct JpegContext {
  JpegContext(GfxRenderer& renderer, const RenderConfig& config) : pipeline(renderer, config) {}
  ImagePipeline pipeline;
  uint32_t lastYieldMs = 0;
};

// File I/O callbacks use pFile->fHandle to access the HalFile*,
// avoiding the need for global file state.
void* jpegOpen(const char* filename, int32_t* size) {
  // JPEGDEC releases this handle through jpegClose().
  HalFile* f = new (std::nothrow) HalFile();
  if (!f) {
    LOG_ERR("JPG", "OOM: JPEG file handle");
    return nullptr;
  }
  if (!Storage.openFileForRead("JPG", std::string(filename), *f)) {
    delete f;
    return nullptr;
  }
  *size = f->size();
  return f;
}

void jpegClose(void* handle) {
  HalFile* f = reinterpret_cast<HalFile*>(handle);
  if (f) {
    f->close();
    delete f;
  }
}

// JPEGDEC tracks file position via pFile->iPos internally (e.g. JPEGGetMoreData
// checks iPos < iSize to decide whether more data is available). The callbacks
// MUST maintain iPos to match the actual file position, otherwise progressive
// JPEGs with large headers fail during parsing.
int32_t jpegRead(JPEGFILE* pFile, uint8_t* pBuf, int32_t len) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return 0;
  int32_t bytesRead = f->read(pBuf, len);
  if (bytesRead < 0) return 0;
  pFile->iPos += bytesRead;
  return bytesRead;
}

int32_t jpegSeek(JPEGFILE* pFile, int32_t pos) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return -1;
  if (!f->seek(pos)) return -1;
  pFile->iPos = pos;
  return pos;
}

// JPEGDEC object is ~17 KB due to internal decode buffers.
// Heap-allocate on demand so memory is only used during active decode.
constexpr size_t JPEG_DECODER_APPROX_SIZE = 20 * 1024;
constexpr size_t MIN_FREE_HEAP_FOR_JPEG = JPEG_DECODER_APPROX_SIZE + 16 * 1024;

int jpegDrawCallback(JPEGDRAW* draw) {
  auto* ctx = static_cast<JpegContext*>(draw->pUser);
  if (!ctx) return 0;
  ImageToFramebufferDecoder::yieldDuringDecode(ctx->lastYieldMs);
  const XpImageRow row{draw->x,
                       draw->y,
                       draw->iWidthUsed,
                       draw->iHeight,
                       draw->iWidth,
                       reinterpret_cast<const uint8_t*>(draw->pPixels),
                       static_cast<size_t>(draw->iWidth) * draw->iHeight,
                       nullptr,
                       0,
                       0,
                       0,
                       8,
                       0};
  return ctx->pipeline.row(row) ? 1 : 0;
}

}  // namespace

bool JpegToFramebufferConverter::getDimensionsStatic(const std::string& imagePath, ImageDimensions& out) {
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_JPEG) {
    LOG_ERR("JPG", "Not enough heap for JPEG decoder (%u free, need %u)", freeHeap, MIN_FREE_HEAP_FOR_JPEG);
    return false;
  }

  std::unique_ptr<JPEGDEC> jpeg(new (std::nothrow) JPEGDEC());
  if (!jpeg) {
    LOG_ERR("JPG", "Failed to allocate JPEG decoder for dimensions");
    return false;
  }

  int rc = jpeg->open(imagePath.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek, nullptr);
  const ScopedCleanup cleanup{[&jpeg]() { jpeg->close(); }};
  if (rc != 1) {
    LOG_ERR("JPG", "Failed to open JPEG for dimensions (err=%d): %s", jpeg->getLastError(), imagePath.c_str());
    return false;
  }

  const int width = jpeg->getWidth();
  const int height = jpeg->getHeight();
  if (!validateAndStoreDimensions(width, height, out, "JPEG")) return false;
  LOG_DBG("JPG", "Image dimensions: %dx%d", width, height);

  return true;
}

bool JpegToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                     const RenderConfig& config) {
  bool cacheWritten;
  return decodeToFramebuffer(imagePath, renderer, config, cacheWritten);
}

bool JpegToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                     const RenderConfig& config, bool& cacheWritten) {
  cacheWritten = false;
  LOG_DBG("JPG", "Decoding JPEG: %s", imagePath.c_str());

  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_JPEG) {
    LOG_ERR("JPG", "Not enough heap for JPEG decoder (%u free, need %u)", freeHeap, MIN_FREE_HEAP_FOR_JPEG);
    return false;
  }

  std::unique_ptr<JPEGDEC> jpeg(new (std::nothrow) JPEGDEC());
  if (!jpeg) {
    LOG_ERR("JPG", "Failed to allocate JPEG decoder");
    return false;
  }

  JpegContext ctx(renderer, config);

  int rc = jpeg->open(imagePath.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek, jpegDrawCallback);
  const ScopedCleanup cleanup{[&jpeg]() { jpeg->close(); }};
  if (rc != 1) {
    LOG_ERR("JPG", "Failed to open JPEG (err=%d): %s", jpeg->getLastError(), imagePath.c_str());
    return false;
  }

  ImageDimensions sourceDimensions;
  if (!validateAndStoreDimensions(jpeg->getWidth(), jpeg->getHeight(), sourceDimensions, "JPEG")) return false;
  const bool progressive = jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE;
  if (progressive) {
    LOG_INF("JPG", "Progressive JPEG detected - decoding DC coefficients only (lower quality)");
  }
  if (!ctx.pipeline.init(sourceDimensions.width, sourceDimensions.height, ImagePipeline::Kind::Jpeg, progressive,
                         imagePath.c_str()) ||
      !ctx.pipeline.prepare())
    return false;
  int jpegScaleOption = 0;
  switch (ctx.pipeline.plan.denominator) {
    case 2:
      jpegScaleOption = JPEG_SCALE_HALF;
      break;
    case 4:
      jpegScaleOption = JPEG_SCALE_QUARTER;
      break;
    case 8:
      jpegScaleOption = JPEG_SCALE_EIGHTH;
      break;
  }
  jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
  jpeg->setUserPointer(&ctx);
  if (!config.cachePath.empty() && !ctx.pipeline.beginCache()) {
    LOG_ERR("JPG", "Failed to start cache stream, continuing without caching");
  }

  unsigned long decodeStart = millis();
  ctx.lastYieldMs = decodeStart;
  rc = jpeg->decode(0, 0, jpegScaleOption);
  unsigned long decodeTime = millis() - decodeStart;

  if (rc != 1) {
    LOG_ERR("JPG", "Decode failed (rc=%d, lastError=%d)", rc, jpeg->getLastError());
    return false;
  }

  LOG_DBG("JPG", "JPEG decoding complete - render time: %lu ms", decodeTime);

  bool success = true;
  cacheWritten = ctx.pipeline.finish(success);
  if (!success) return false;

  return true;
}

bool JpegToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasJpgExtension(extension);
}
