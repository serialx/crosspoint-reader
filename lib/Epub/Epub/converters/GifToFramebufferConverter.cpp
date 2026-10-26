#include "GifToFramebufferConverter.h"

#include <AnimatedGIF.h>
#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <GifCommon.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include "ImagePipeline.h"

namespace {

struct GifContext {
  GifContext(GfxRenderer& renderer, const RenderConfig& config) : pipeline(renderer, config) {}
  ImagePipeline pipeline;
  const uint8_t* activePalette24 = nullptr;
  uint8_t grayPalette[256] = {};
  bool success = true;
  uint32_t lastYieldMs = 0;
};

void* gifOpenWithHandle(const char* filename, int32_t* size) {
  auto file = makeUniqueNoThrow<HalFile>();
  if (!file) {
    LOG_ERR("GIF", "OOM: file handle for %s", filename);
    return nullptr;
  }
  if (!Storage.openFileForRead("GIF", std::string(filename), *file)) {
    LOG_ERR("GIF", "Failed to open decoder source: %s", filename);
    return nullptr;
  }
  *size = static_cast<int32_t>(file->size());
  return file.release();
}

void gifCloseWithHandle(void* handle) {
  auto* file = reinterpret_cast<HalFile*>(handle);
  if (!file) return;
  file->close();
  delete file;
}

int32_t gifReadWithHandle(GIFFILE* pFile, uint8_t* pBuf, int32_t len) {
  auto* file = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!file) {
    LOG_ERR("GIF", "Read requested without a file handle");
    return 0;
  }
  int32_t bytesRead = file->read(pBuf, len);
  if (bytesRead < 0) {
    LOG_ERR("GIF", "Read failed at offset %d for %d bytes", pFile->iPos, len);
    return 0;
  }
  pFile->iPos += bytesRead;
  return bytesRead;
}

int32_t gifSeekWithHandle(GIFFILE* pFile, int32_t position) {
  auto* file = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!file) {
    LOG_ERR("GIF", "Seek requested without a file handle");
    return -1;
  }
  if (position < 0) position = 0;
  if (position >= pFile->iSize) position = pFile->iSize - 1;
  if (!file->seek(position)) {
    LOG_ERR("GIF", "Seek failed at offset %d", position);
    return -1;
  }
  pFile->iPos = position;
  return position;
}

void gifDrawCallback(GIFDRAW* draw) {
  auto* ctx = static_cast<GifContext*>(draw->pUser);
  if (!ctx || !ctx->success) return;
  ImageToFramebufferDecoder::yieldDuringDecode(ctx->lastYieldMs);
  if (draw->pPalette24 && draw->pPalette24 != ctx->activePalette24) {
    xp_image_gray_palette(draw->pPalette24, ctx->grayPalette);
    ctx->activePalette24 = draw->pPalette24;
  }
  const XpImageRow row{draw->iX,
                       draw->iY + draw->y,
                       draw->iWidth,
                       1,
                       draw->iWidth,
                       draw->pPixels,
                       static_cast<size_t>(draw->iWidth),
                       ctx->grayPalette,
                       sizeof(ctx->grayPalette),
                       draw->ucTransparent,
                       3,
                       8,
                       draw->ucHasTransparency};
  ctx->success = ctx->pipeline.row(row);
}

bool readGifInfoFromPath(const std::string& imagePath, GifBasicInfo& info) {
  HalFile file;
  if (!Storage.openFileForRead("GIF", imagePath, file)) {
    LOG_ERR("GIF", "Failed to open source for header inspection: %s", imagePath.c_str());
    return false;
  }
  return GifCommon::readBasicInfo(file, info);
}

constexpr size_t GIF_DECODER_APPROX_SIZE = sizeof(AnimatedGIF);
constexpr size_t MIN_FREE_HEAP_FOR_GIF = GIF_DECODER_APPROX_SIZE + 16 * 1024;

}  // namespace

bool GifToFramebufferConverter::getDimensionsStatic(const std::string& imagePath, ImageDimensions& out) {
  GifBasicInfo info;
  if (!readGifInfoFromPath(imagePath, info)) {
    LOG_ERR("GIF", "Failed to read GIF dimensions: %s", imagePath.c_str());
    return false;
  }
  return validateAndStoreDimensions(info.canvasWidth, info.canvasHeight, out, "GIF");
}

bool GifToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                    const RenderConfig& config) {
  LOG_DBG("GIF", "Decoding GIF: %s", imagePath.c_str());

  GifBasicInfo info;
  if (!readGifInfoFromPath(imagePath, info)) {
    LOG_ERR("GIF", "Failed to parse GIF header: %s", imagePath.c_str());
    return false;
  }
  if (info.interlaced) {
    warnUnsupportedFeature("interlaced GIF", imagePath);
    return false;
  }
  if (info.canvasWidth > MAX_WIDTH) {
    LOG_ERR("GIF",
            "GIF canvas too wide for AnimatedGIF: canvas=%ux%u MAX_WIDTH=%d "
            "renderTarget=%dx%d file=%s",
            info.canvasWidth, info.canvasHeight, MAX_WIDTH, config.maxWidth, config.maxHeight, imagePath.c_str());
    return false;
  }
  ImageDimensions sourceDimensions;
  if (!validateAndStoreDimensions(info.canvasWidth, info.canvasHeight, sourceDimensions, "GIF")) return false;

  const size_t freeHeap = HalMemory::getInternalHeap().freeBytes;
  if (freeHeap < MIN_FREE_HEAP_FOR_GIF) {
    LOG_ERR("GIF", "Not enough heap for GIF decoder (%u free, need %u)", freeHeap, MIN_FREE_HEAP_FOR_GIF);
    return false;
  }

  auto gif = makeUniqueNoThrow<AnimatedGIF>();
  if (!gif) {
    LOG_ERR("GIF", "OOM: AnimatedGIF decoder");
    return false;
  }

  auto ctxPtr = makeUniqueNoThrow<GifContext>(renderer, config);
  if (!ctxPtr) {
    LOG_ERR("GIF", "OOM: GIF context");
    return false;
  }
  GifContext& ctx = *ctxPtr;
  if (!ctx.pipeline.init(sourceDimensions.width, sourceDimensions.height, ImagePipeline::Kind::Gif) ||
      !ctx.pipeline.prepare())
    return false;

  gif->begin(GIF_PALETTE_RGB888);
  gif->setDrawType(GIF_DRAW_RAW);
  LOG_DBG("GIF",
          "GIF header: canvas=%ux%u frame=%ux%u frameOffset=%u,%u interlaced=%s "
          "renderTarget=%dx%d MAX_WIDTH=%d freeHeap=%u file=%s",
          info.canvasWidth, info.canvasHeight, info.frameWidth, info.frameHeight, info.frameX, info.frameY,
          info.interlaced ? "yes" : "no", config.maxWidth, config.maxHeight, MAX_WIDTH,
          HalMemory::getInternalHeap().freeBytes, imagePath.c_str());
  const int rcOpen = gif->open(imagePath.c_str(), gifOpenWithHandle, gifCloseWithHandle, gifReadWithHandle,
                               gifSeekWithHandle, gifDrawCallback);
  const ScopedCleanup cleanup{[&gif]() { gif->close(); }};
  if (rcOpen != 1) {
    const int err = gif->getLastError();
    LOG_ERR("GIF", "AnimatedGIF open failed: err=%d canvas=%ux%u MAX_WIDTH=%d file=%s", err, info.canvasWidth,
            info.canvasHeight, MAX_WIDTH, imagePath.c_str());
    return false;
  }

  const bool caching = !config.cachePath.empty();
  if (caching && !ctx.pipeline.beginCache()) {
    LOG_ERR("GIF", "Failed to start cache stream");
    return false;
  }

  int delayMs = 0;
  ctx.lastYieldMs = millis();
  const int frameState = gif->playFrame(false, &delayMs, &ctx);
  if (!ctx.success || (gif->getLastError() != GIF_SUCCESS && gif->getLastError() != GIF_EMPTY_FRAME)) {
    LOG_ERR("GIF", "GIF decode failed (state=%d, err=%d)", frameState, gif->getLastError());
    return false;
  }

  if (frameState > 0) {
    warnUnsupportedFeature("animation (using first frame only)", imagePath);
  }

  const bool cached = ctx.pipeline.finish(ctx.success);
  if (!ctx.success || (caching && !cached)) {
    LOG_ERR("GIF", "Failed to finish image/cache: %s", config.cachePath.c_str());
    return false;
  }

  return true;
}

bool GifToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasGifExtension(extension);
}
