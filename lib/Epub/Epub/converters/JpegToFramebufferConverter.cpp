#include "JpegToFramebufferConverter.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <JPEGDEC.h>
#include <Logging.h>
#include <Memory.h>
#include <xp_book.h>
#include <xp_page_paint.h>

#include <memory>
#include <new>
#include <optional>

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

void logJpegDiagnostic(const char* path, int width, int height, int error, int result, uint32_t event,
                       uint32_t argument) {
  switch (static_cast<XpBookJpegEvent>(event)) {
    case XP_BOOK_JPEG_EVENT_HEAP:
      LOG_ERR("JPG", "Not enough heap for JPEG decoder (%u free, need %u)", argument, MIN_FREE_HEAP_FOR_JPEG);
      break;
    case XP_BOOK_JPEG_EVENT_DIMENSION_HEAP:
      LOG_ERR("JPG", "Not enough heap for JPEG decoder (%u free, need %u)", argument, MIN_FREE_HEAP_FOR_JPEG);
      break;
    case XP_BOOK_JPEG_EVENT_DIMENSION_ALLOCATION:
      LOG_ERR("JPG", "Failed to allocate JPEG decoder for dimensions");
      break;
    case XP_BOOK_JPEG_EVENT_DIMENSION_OPEN:
      LOG_ERR("JPG", "Failed to open JPEG for dimensions (err=%d): %s", error, path);
      break;
    case XP_BOOK_JPEG_EVENT_DIMENSIONS:
      LOG_DBG("JPG", "Image dimensions: %dx%d", width, height);
      break;
    case XP_BOOK_JPEG_EVENT_RENDER:
      LOG_DBG("JPG", "Decoding JPEG: %s", path);
      break;
    case XP_BOOK_JPEG_EVENT_ALLOCATION:
      LOG_ERR("JPG", "Failed to allocate JPEG decoder");
      break;
    case XP_BOOK_JPEG_EVENT_OPEN:
      LOG_ERR("JPG", "Failed to open JPEG (err=%d): %s", error, path);
      break;
    case XP_BOOK_JPEG_EVENT_PROGRESSIVE:
      LOG_INF("JPG", "Progressive JPEG detected - decoding DC coefficients only (lower quality)");
      break;
    case XP_BOOK_JPEG_EVENT_CACHE:
      LOG_ERR("JPG", "Failed to start cache stream, continuing without caching");
      break;
    case XP_BOOK_JPEG_EVENT_DECODE:
      LOG_ERR("JPG", "Decode failed (rc=%d, lastError=%d)", result, error);
      break;
    case XP_BOOK_JPEG_EVENT_TIME:
      break;
  }
}

extern "C" int32_t xp_page_image_jpeg_row(void*, const XpImageRow*);
int readerJpegDraw(JPEGDRAW* draw) {
  if (!draw->pUser) return 0;
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
  return xp_page_image_jpeg_row(draw->pUser, &row);
}

struct JpegDimensionsContext {
  const std::string& path;
  ImageDimensions& dimensions;
  std::unique_ptr<JPEGDEC> jpeg;
  bool opened = false;
  int result = 0;
  explicit JpegDimensionsContext(const std::string& path, ImageDimensions& dimensions)
      : path(path), dimensions(dimensions) {}
  void close() {
    if (opened) {
      jpeg->close();
      opened = false;
    }
  }
  ~JpegDimensionsContext() { close(); }
  static uint32_t operation(void* raw, uint32_t operation, uint32_t argument) {
    auto& c = *static_cast<JpegDimensionsContext*>(raw);
    switch (static_cast<XpBookJpegOperation>(operation)) {
      case XP_BOOK_JPEG_HEAP:
        return ESP.getFreeHeap();
      case XP_BOOK_JPEG_ALLOCATE:
        c.jpeg.reset(new (std::nothrow) JPEGDEC());
        return bool(c.jpeg);
      case XP_BOOK_JPEG_OPEN:
        c.result = c.jpeg->open(c.path.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek,
                                argument ? jpegDrawCallback : nullptr);
        c.opened = true;
        return c.result == 1;
      case XP_BOOK_JPEG_VALIDATE:
        return ImageToFramebufferDecoder::validateAndStoreDimensions(c.jpeg->getWidth(), c.jpeg->getHeight(),
                                                                     c.dimensions, "JPEG");
      case XP_BOOK_JPEG_PROGRESSIVE:
        return c.jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE;
      default:
        return 0;
    }
  }
  static void diagnostic(void* raw, uint32_t event, uint32_t argument) {
    auto& c = *static_cast<JpegDimensionsContext*>(raw);
    logJpegDiagnostic(c.path.c_str(), c.dimensions.width, c.dimensions.height, c.jpeg ? c.jpeg->getLastError() : 0,
                      c.result, event, argument);
  }
};
struct JpegRenderContext : JpegDimensionsContext {
  GfxRenderer& renderer;
  const RenderConfig& config;
  bool& cacheWritten;
  std::optional<JpegContext> drawing;
  unsigned long started = 0, elapsed = 0;
  bool success = true;
  int scale = 0;
  JpegRenderContext(const std::string& path, ImageDimensions& dimensions, GfxRenderer& renderer,
                    const RenderConfig& config, bool& cacheWritten)
      : JpegDimensionsContext(path, dimensions), renderer(renderer), config(config), cacheWritten(cacheWritten) {}
  ~JpegRenderContext() { close(); }
  static uint32_t operation(void* raw, uint32_t operation, uint32_t argument) {
    auto& c = *static_cast<JpegRenderContext*>(raw);
    switch (static_cast<XpBookJpegOperation>(operation)) {
      case XP_BOOK_JPEG_DRAWING:
        c.drawing.emplace(c.renderer, c.config);
        break;
      case XP_BOOK_JPEG_PIPELINE_INIT:
        return c.drawing->pipeline.init(c.dimensions.width, c.dimensions.height, ImagePipeline::Kind::Jpeg, argument,
                                        c.path.c_str());
      case XP_BOOK_JPEG_PREPARE:
        return c.drawing->pipeline.prepare();
      case XP_BOOK_JPEG_DENOMINATOR:
        return c.drawing->pipeline.plan.denominator;
      case XP_BOOK_JPEG_CONFIGURE:
        switch (argument) {
          case 2:
            c.scale = JPEG_SCALE_HALF;
            break;
          case 4:
            c.scale = JPEG_SCALE_QUARTER;
            break;
          case 8:
            c.scale = JPEG_SCALE_EIGHTH;
            break;
          default:
            c.scale = 0;
            break;
        }
        c.jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
        c.jpeg->setUserPointer(&*c.drawing);
        break;
      case XP_BOOK_JPEG_HAS_CACHE:
        return !c.config.cachePath.empty();
      case XP_BOOK_JPEG_BEGIN_CACHE:
        return c.drawing->pipeline.beginCache();
      case XP_BOOK_JPEG_BEGIN_TIME:
        c.started = millis();
        c.drawing->lastYieldMs = c.started;
        break;
      case XP_BOOK_JPEG_DECODE:
        c.result = c.jpeg->decode(0, 0, c.scale);
        return c.result == 1;
      case XP_BOOK_JPEG_END_TIME:
        c.elapsed = millis() - c.started;
        break;
      case XP_BOOK_JPEG_FINISH:
        c.cacheWritten = c.drawing->pipeline.finish(c.success);
        break;
      case XP_BOOK_JPEG_SUCCESS:
        return c.success;
      default:
        return JpegDimensionsContext::operation(static_cast<JpegDimensionsContext*>(&c), operation, argument);
    }
    return 0;
  }
  static void diagnostic(void* raw, uint32_t event, uint32_t argument) {
    auto& c = *static_cast<JpegRenderContext*>(raw);
    if (event == XP_BOOK_JPEG_EVENT_TIME) {
      LOG_DBG("JPG", "JPEG decoding complete - render time: %lu ms", c.elapsed);
    } else {
      JpegDimensionsContext::diagnostic(static_cast<JpegDimensionsContext*>(&c), event, argument);
    }
  }
};
}  // namespace

bool JpegToFramebufferConverter::getDimensionsStatic(const std::string& imagePath, ImageDimensions& out) {
  JpegDimensionsContext context{imagePath, out};
  const XpBookServices services{&context, JpegDimensionsContext::operation, nullptr, JpegDimensionsContext::diagnostic};
  return xp_book_jpeg(&services, false);
}
bool JpegToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                     const RenderConfig& config) {
  bool cacheWritten;
  return decodeToFramebuffer(imagePath, renderer, config, cacheWritten);
}
bool JpegToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                     const RenderConfig& config, bool& cacheWritten) {
  cacheWritten = false;
  ImageDimensions dimensions;
  JpegRenderContext context{imagePath, dimensions, renderer, config, cacheWritten};
  const XpBookServices services{&context, JpegRenderContext::operation, nullptr, JpegRenderContext::diagnostic};
  return xp_book_jpeg(&services, true);
}

bool JpegToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasJpgExtension(extension);
}

extern "C" uint32_t xp_native_page_jpeg(XpPageJpeg* state, XpPageJpegOp operation, uint32_t argument) {
  auto* jpeg = static_cast<JPEGDEC*>(state->decoder);
  switch (operation) {
    case XP_PAGE_JPEG_ALLOCATE:
      state->decoder = new (std::nothrow) JPEGDEC();
      return state->decoder != nullptr;
    case XP_PAGE_JPEG_OPEN:
      state->result = jpeg->open(reinterpret_cast<const char*>(state->path), jpegOpen, jpegClose, jpegRead, jpegSeek,
                                 state->rows ? readerJpegDraw : nullptr);
      state->error = jpeg->getLastError();
      return state->result == 1;
    case XP_PAGE_JPEG_FACTS:
      state->width = jpeg->getWidth();
      state->height = jpeg->getHeight();
      state->progressive = jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE;
      break;
    case XP_PAGE_JPEG_CONFIGURE:
      jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
      jpeg->setUserPointer(state->rows);
      break;
    case XP_PAGE_JPEG_DECODE: {
      int scale = 0;
      switch (argument) {
        case 2:
          scale = JPEG_SCALE_HALF;
          break;
        case 4:
          scale = JPEG_SCALE_QUARTER;
          break;
        case 8:
          scale = JPEG_SCALE_EIGHTH;
          break;
        default:
          break;
      }
      state->result = jpeg->decode(0, 0, scale);
      state->error = jpeg->getLastError();
      return state->result == 1;
    }
    case XP_PAGE_JPEG_CLOSE:
      jpeg->close();
      break;
    case XP_PAGE_JPEG_FREE:
      delete jpeg;
      state->decoder = nullptr;
      break;
  }
  return 0;
}
extern "C" void xp_native_page_jpeg_log(const XpPageJpeg* state, uint32_t event, uint32_t argument) {
  if (event == XP_BOOK_JPEG_EVENT_TIME) {
    LOG_DBG("JPG", "JPEG decoding complete - render time: %lu ms", static_cast<unsigned long>(argument));
  } else if (event == 12) {
    const auto width = static_cast<long long>(state->width), height = static_cast<long long>(state->height);
    if (argument == 1) {
      LOG_ERR("IMG", "Invalid %s dimensions: %lldx%lld", "JPEG", width, height);
    } else if (argument == 2) {
      LOG_ERR("IMG", "%s dimensions exceed supported limit: %lldx%lld (max %lld per dimension)", "JPEG", width, height,
              32767LL);
    } else if (argument == 3) {
      LOG_ERR("IMG", "%s too large (%lldx%lld = %lld pixels), max supported: %lld pixels", "JPEG", width, height,
              width * height, 8388608LL);
    }
  } else {
    logJpegDiagnostic(reinterpret_cast<const char*>(state->path), state->width, state->height, state->error,
                      state->result, event, argument);
  }
}
