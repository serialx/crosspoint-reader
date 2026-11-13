#include "ImageBlock.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <GifCommon.h>
#include <HalMemory.h>
#include <Logging.h>
#include <PngDiagnostics.h>
#include <xp_book.h>
#include <xp_ffi.h>

#include "Epub/converters/ImagePipeline.h"
ImageBlock::ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height)
    : ownedImagePath(imagePath),
      ownedSrcPath(srcPath),
      imagePath(ownedImagePath),
      srcPath(ownedSrcPath),
      width(width),
      height(height) {}

namespace {
enum class ImageDiagnostic : uint32_t {
  InvalidCache = 0,
  CacheLoading = 1,
  CacheLoaded = 2,
  RowAllocation = 3,
  CacheRead = 4,
  CacheComplete = 5,
  Render = 6,
  Bounds = 7,
  MissingFile = 8,
  EmptyFile = 9,
  Decode = 10,
  MissingDecoder = 11,
  DecodeFailed = 12,
  DecodeComplete = 13,
  ExtractBegin = 14,
  ExtractFailed = 15,
  Unsupported = 16
};

struct Context {
  GfxRenderer* renderer;
  std::string_view path, source;
  int x, y, width, height;
  const char* cache = "";
};

void diagnostic(void* raw, uint32_t event, [[maybe_unused]] int32_t a, [[maybe_unused]] int32_t b) {
  [[maybe_unused]] auto& self = *static_cast<Context*>(raw);
  switch (static_cast<ImageDiagnostic>(event)) {
    case ImageDiagnostic::Unsupported:
      LOG_ERR("DEC", "No decoder found for image: %s", self.path.data());
      break;
    case ImageDiagnostic::ExtractBegin:
      LOG_DBG("IMG", "Lazy-extracting %s -> %s", self.source.data(), self.path.data());
      break;
    case ImageDiagnostic::ExtractFailed:
      LOG_ERR("IMG", "Lazy extraction failed: %s", self.source.data());
      break;
    case ImageDiagnostic::InvalidCache:
      LOG_ERR("IMG", "Invalid image cache: %s", self.cache);
      break;
    case ImageDiagnostic::CacheLoading:
      LOG_DBG("IMG", "Loading from cache: %s (%dx%d)", self.cache, a, b);
      break;
    case ImageDiagnostic::CacheLoaded:
      LOG_DBG("IMG", "Cache render complete (payload now in RAM)");
      break;
    case ImageDiagnostic::RowAllocation:
      LOG_ERR("IMG", "Failed to allocate row buffer");
      break;
    case ImageDiagnostic::CacheRead:
      LOG_ERR("IMG", "Cache read error at row %d", a);
      break;
    case ImageDiagnostic::CacheComplete:
      LOG_DBG("IMG", "Cache render complete");
      break;
    case ImageDiagnostic::Render:
      LOG_DBG("IMG", "Rendering image at %d,%d: %s (%dx%d)", self.x, self.y, self.path.data(), self.width, self.height);
      break;
    case ImageDiagnostic::Bounds:
      LOG_ERR("IMG", "Invalid render position: (%d,%d) size (%dx%d) screen (%dx%d)", self.x, self.y, self.width,
              self.height, self.renderer->getScreenWidth(), self.renderer->getScreenHeight());
      break;
    case ImageDiagnostic::MissingFile:
      LOG_ERR("IMG", "Image file not found: %s", self.path.data());
      break;
    case ImageDiagnostic::EmptyFile:
      LOG_ERR("IMG", "Image file is empty: %s", self.path.data());
      break;
    case ImageDiagnostic::Decode:
      LOG_DBG("IMG", "Decoding and caching: %s", self.path.data());
      break;
    case ImageDiagnostic::MissingDecoder:
      LOG_ERR("IMG", "No decoder found for image: %s", self.path.data());
      break;
    case ImageDiagnostic::DecodeFailed:
      LOG_ERR("IMG", "Failed to decode image: %s", self.path.data());
      break;
    case ImageDiagnostic::DecodeComplete:
      LOG_DBG("IMG", "Decode successful");
      break;
  }
}
}  // namespace

extern "C" size_t xp_native_page_image_file(XpPageImageFile* storage, XpPageImageFileOp operation, const uint8_t* path,
                                            uint8_t* bytes, size_t value) {
  static_assert(sizeof(HalFile) <= sizeof(XpPageImageFile));
  static_assert(alignof(HalFile) <= alignof(XpPageImageFile));
  auto* file = reinterpret_cast<HalFile*>(storage);
  switch (operation) {
    case XP_IMAGE_FILE_INIT:
      new (storage) HalFile();
      return 0;
    case XP_IMAGE_FILE_DROP:
      file->~HalFile();
      return 0;
    case XP_IMAGE_FILE_OPEN:
      return Storage.openFileForRead("IMG", reinterpret_cast<const char*>(path), *file);
    case XP_IMAGE_FILE_READ: {
      const int count = file->read(bytes, value);
      return count > 0 ? count : 0;
    }
    case XP_IMAGE_FILE_SEEK:
      return file->seek(value);
    case XP_IMAGE_FILE_SIZE:
      return file->size();
    case XP_IMAGE_FILE_CLOSE:
      return file->close();
    case XP_IMAGE_FILE_EXISTS:
      return Storage.exists(reinterpret_cast<const char*>(path));
    case XP_IMAGE_FILE_FREE_HEAP:
      return ESP.getFreeHeap();
    case XP_IMAGE_FILE_LARGEST_BLOCK:
      return ESP.getMaxAllocHeap();
    case XP_IMAGE_FILE_OPEN_WRITE:
      return Storage.openFileForWrite("EBP", reinterpret_cast<const char*>(path), *file);
    case XP_IMAGE_FILE_FLUSH:
      file->flush();
      return 0;
    case XP_IMAGE_FILE_REMOVE:
      return Storage.remove(reinterpret_cast<const char*>(path));
    case XP_IMAGE_FILE_POSITION:
      return file->position();
    case XP_IMAGE_FILE_READ_RAW:
      return static_cast<size_t>(file->read(bytes, value));
    case XP_IMAGE_FILE_OPEN_PNG:
      return Storage.openFileForRead("PNG", reinterpret_cast<const char*>(path), *file);
    case XP_IMAGE_FILE_OPEN_GIF:
      return Storage.openFileForRead("GIF", reinterpret_cast<const char*>(path), *file);
    case XP_IMAGE_FILE_OPEN_CACHE_WRITE:
      return Storage.openFileForWrite("IMG", reinterpret_cast<const char*>(path), *file);
    case XP_IMAGE_FILE_IS_OPEN:
      return file->isOpen();
    case XP_IMAGE_FILE_NOW:
      return millis();
    case XP_IMAGE_FILE_YIELD:
      vTaskDelay(1);
      return 0;
    case XP_IMAGE_FILE_FREE_INTERNAL_HEAP:
      return HalMemory::getInternalHeap().freeBytes;
  }
  return 0;
}
extern "C" uint8_t xp_native_page_image_write(void* file, const uint8_t* bytes, size_t length) {
  return static_cast<HalFile*>(file)->write(bytes, length) == length;
}
extern "C" void xp_native_page_image_codec_log(const XpPageImageCodecLog* state, uint32_t event, uint32_t a, uint32_t b,
                                               uint32_t c, uint32_t d) {
  enum class Format : uint8_t { Png, Jpeg, Gif };
  const auto& image = *state->image;
  const auto* path = reinterpret_cast<const char*>(image.path);
  const auto* cache = reinterpret_cast<const char*>(state->cache);
  if (event == XP_IMAGE_CODEC_PROBE_OPEN || event == XP_IMAGE_CODEC_PROBE_READ) {
    if (static_cast<Format>(state->format) == Format::Png) {
      LOG_ERR("PNG", "Failed to open PNG for dimensions");
    } else if (event == XP_IMAGE_CODEC_PROBE_OPEN) {
      LOG_ERR("GIF", "Failed to open source for header inspection: %s", path);
    } else {
      LOG_ERR("GIF", "Failed to read GIF dimensions: %s", path);
    }
  } else if (event == XP_IMAGE_CODEC_PROBE_VALIDATION) {
    const char* format = static_cast<Format>(state->format) == Format::Png ? "PNG" : "GIF";
    const auto width = static_cast<long long>(static_cast<int32_t>(a));
    const auto height = static_cast<long long>(static_cast<int32_t>(b));
    if (c == 1) {
      LOG_ERR("IMG", "Invalid %s dimensions: %lldx%lld", format, width, height);
    } else if (c == 2) {
      LOG_ERR("IMG", "%s dimensions exceed supported limit: %lldx%lld (max %lld per dimension)", format, width, height,
              32767LL);
    } else if (c == 3) {
      LOG_ERR("IMG", "%s too large (%lldx%lld = %lld pixels), max supported: %lld pixels", format, width, height,
              width * height, 8388608LL);
    }
  } else if (event >= XP_IMAGE_DIAG_CacheBand && event <= XP_IMAGE_DIAG_JpegSummary) {
    logImageDiagnostic(event, a, b, c, d, cache, path, state->color, state->depth);
  } else if (static_cast<Format>(state->format) == Format::Jpeg) {
    if (event == XP_IMAGE_CODEC_BEGIN) LOG_DBG("IMG", "Using %s decoder", "JPEG");
  } else if (static_cast<Format>(state->format) == Format::Png) {
    switch (event) {
      case XP_IMAGE_CODEC_BEGIN:
        LOG_DBG("IMG", "Using %s decoder", "PNG");
        LOG_DBG("PNG", "Decoding PNG: %s", path);
        break;
      case XP_IMAGE_CODEC_OPEN:
        LOG_ERR("PNG", "Failed to open PNG");
        break;
      case XP_PNG_DIAG_DecodeComplete:
        LOG_DBG("PNG", "PNG decoding complete - render time: %lu ms", static_cast<unsigned long>(state->elapsed));
        break;
      default:
        logPngDiagnostic(event, a, b, c, d, image.width, image.height, path);
        break;
    }
  } else {
    switch (event) {
      case XP_IMAGE_CODEC_BEGIN:
        LOG_DBG("IMG", "Using %s decoder", "GIF");
        LOG_DBG("GIF", "Decoding GIF: %s", path);
        break;
      case XP_IMAGE_CODEC_OPEN:
        LOG_ERR("GIF", "Failed to open source for header inspection: %s", path);
        LOG_ERR("GIF", "Failed to parse GIF header: %s", path);
        break;
      case XP_IMAGE_CODEC_SEEK:
        LOG_ERR("GIF", "Seek failed at offset %d", static_cast<int>(a));
        break;
      case XP_IMAGE_CODEC_READ:
        LOG_ERR("GIF", "Read failed at offset %d for %d bytes", static_cast<int>(a), static_cast<int>(b));
        break;
      default: {
        GifCommon::Source source{*reinterpret_cast<HalFile*>(state->input)};
        source.path = path;
        source.cachePath = cache;
        source.targetWidth = image.width;
        source.targetHeight = image.height;
        source.freeHeap = state->free_heap;
        source.seekFailed = state->seek_failed != 0;
        GifCommon::Source::diagnostic(&source, event, a, b, c, d);
        break;
      }
    }
  }
}

extern "C" void xp_native_page_image_log(void* raw, const XpPageImage* image, const uint8_t* cache, uint32_t event,
                                         int32_t a, int32_t b) {
  Context context{static_cast<GfxRenderer*>(raw),
                  {reinterpret_cast<const char*>(image->path), image->path_len},
                  {reinterpret_cast<const char*>(image->source), image->source_len},
                  image->x,
                  image->y,
                  image->width,
                  image->height};
  context.cache = reinterpret_cast<const char*>(cache);
  diagnostic(&context, event, a, b);
}

#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
void ImageBlock::setRenderReadWindow(uint8_t* data, size_t size) { xp_assets_image_read_window(data, size); }
#endif
