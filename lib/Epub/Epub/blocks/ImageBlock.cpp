#include "ImageBlock.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <xp_book.h>
#include <xp_ffi.h>

#include "Epub/converters/ImageDecoderFactory.h"
#include "Epub/converters/ImagePipeline.h"
ImageBlock::ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height)
    : ownedImagePath(imagePath),
      ownedSrcPath(srcPath),
      imagePath(ownedImagePath),
      srcPath(ownedSrcPath),
      width(width),
      height(height) {}

void* ImageBlock::extractCtx = nullptr;
ImageBlock::ExtractFn ImageBlock::extractFn = nullptr;

void ImageBlock::setExtractor(void* ctx, ExtractFn fn) {
  extractCtx = ctx;
  extractFn = fn;
}

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
  ExtractFailed = 15
};
enum class ImageIo : uint32_t {
  OpenCache,
  Read,
  Seek,
  Size,
  Close,
  FreeHeap,
  LargestBlock,
  Extract,
  SourceSize,
  Decode,
  HasSource,
  HasExtractor,
  SourceExists
};
enum class ImageResult : uint32_t { NoPixels, Preserve, Placeholder };
struct Context {
  GfxRenderer* renderer;
  std::string_view path, source;
  int x, y, width, height;
  void* extractContext;
  ImageBlock::ExtractFn extract;
  HalFile file{};
  const char* cache = "";
};
size_t io(void* raw, uint32_t op, const uint8_t* path, uint8_t* bytes, size_t len) {
  auto& self = *static_cast<Context*>(raw);
  switch (static_cast<ImageIo>(op)) {
    case ImageIo::OpenCache:
      self.cache = reinterpret_cast<const char*>(path);
      return Storage.openFileForRead("IMG", self.cache, self.file);
    case ImageIo::Read: {
      const int count = self.file.read(bytes, len);
      return count > 0 ? static_cast<size_t>(count) : 0;
    }
    case ImageIo::Seek:
      return self.file.seek(len);
    case ImageIo::Size:
      return self.file.size();
    case ImageIo::Close:
      self.file.close();
      return 0;
    case ImageIo::FreeHeap:
      return ESP.getFreeHeap();
    case ImageIo::LargestBlock:
      return ESP.getMaxAllocHeap();
    case ImageIo::HasSource:
      return !self.source.empty();
    case ImageIo::HasExtractor:
      return self.extract != nullptr;
    case ImageIo::SourceExists:
      return Storage.exists(self.path.data());
    case ImageIo::Extract:
      return self.extract(self.extractContext, self.source.data(), self.path.data());
    case ImageIo::SourceSize: {
      if (!Storage.openFileForRead("IMG", self.path.data(), self.file)) return SIZE_MAX;
      const size_t size = self.file.size();
      self.file.close();
      return size;
    }
    case ImageIo::Decode: {
      const std::string imagePath(self.path);
      auto* decoder = ImageDecoderFactory::getDecoder(imagePath);
      if (!decoder) return 0;
      LOG_DBG("IMG", "Using %s decoder", decoder->getFormatName());
      RenderConfig config;
      config.x = self.x;
      config.y = self.y;
      config.maxWidth = self.width;
      config.maxHeight = self.height;
      config.useExactDimensions = true;
      config.cachePath = reinterpret_cast<const char*>(path);
      return decoder->decodeToFramebuffer(imagePath, *self.renderer, config) ? 2 : 1;
    }
  }
  return 0;
}
void target(void* raw, XpImageTarget* output) { *output = imageWriteTarget(*static_cast<Context*>(raw)->renderer); }
void diagnostic(void* raw, uint32_t event, [[maybe_unused]] int32_t a, [[maybe_unused]] int32_t b) {
  [[maybe_unused]] auto& self = *static_cast<Context*>(raw);
  switch (static_cast<ImageDiagnostic>(event)) {
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
bool ImageBlock::hasValidCache() const {
  Context context{nullptr, imagePath, srcPath, 0, 0, width, height, extractCtx, extractFn};
  const XpAssetImageCallbacks services{&context, io, target, diagnostic};
  return xp_assets_image_valid(reinterpret_cast<const uint8_t*>(imagePath.data()), imagePath.size(), width, height,
                               &services);
}
bool ImageBlock::needsDecode() const {
  return !xp_assets_image_failed(reinterpret_cast<const uint8_t*>(imagePath.data()), imagePath.size()) &&
         !hasValidCache();
}
void ImageBlock::clearRenderFailures() { xp_assets_image_reset(false); }
void ImageBlock::releaseRenderCache() { xp_assets_image_reset(true); }
void ImageBlock::renderPlaceholder(GfxRenderer& renderer, const int x, const int y) const {
  xp_book_image_placeholder(x, y, width, height, &renderer,
                            [](void* raw, int32_t x, int32_t y, int32_t width, int32_t height, uint8_t black) {
                              static_cast<GfxRenderer*>(raw)->fillRect(x, y, width, height, black);
                            });
}
void ImageBlock::render(GfxRenderer& renderer, const int x, const int y) {
  auto* fonts = renderer.getFontCacheManager();
  const XpAssetImageConfig config{x,
                                  y,
                                  width,
                                  height,
                                  renderer.getScreenWidth(),
                                  renderer.getScreenHeight(),
                                  fonts && fonts->isScanning(),
                                  renderer.glyphIntersectsStrip(x, y, x + width - 1, y + height - 1)};
  Context context{&renderer, imagePath, srcPath, x, y, width, height, extractCtx, extractFn};
  const XpAssetImageCallbacks services{&context, io, target, diagnostic};
  switch (static_cast<ImageResult>(xp_assets_image_render(reinterpret_cast<const uint8_t*>(imagePath.data()),
                                                          imagePath.size(), &config, &services))) {
    case ImageResult::Preserve:
      renderer.preserveImagePolarity(x, y, width, height);
      break;
    case ImageResult::Placeholder:
      renderPlaceholder(renderer, x, y);
      break;
    case ImageResult::NoPixels:
      break;
  }
}

#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
void ImageBlock::setRenderReadWindow(uint8_t* data, size_t size) { xp_assets_image_read_window(data, size); }
#endif
