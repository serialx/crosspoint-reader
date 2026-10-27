#include "PngToFramebufferConverter.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PngDiagnostics.h>
#include <xp_ffi.h>

#include "ImagePipeline.h"

namespace {
struct Context {
  HalFile input;
  HalFile cache;
  GfxRenderer& renderer;
  const RenderConfig& config;
  const char* path;
  uint32_t lastYieldMs = 0;
  uint32_t decodeStart = 0;
};

size_t readFile(void* context, uint32_t offset, uint8_t* bytes, size_t length) {
  auto& file = *static_cast<HalFile*>(context);
  if (file.position() != offset && !file.seek(offset)) return 0;
  const int count = file.read(bytes, length);
  return count > 0 ? static_cast<size_t>(count) : 0;
}
size_t read(void* context, uint32_t offset, uint8_t* bytes, size_t length) {
  auto& self = *static_cast<Context*>(context);
  return readFile(&self.input, offset, bytes, length);
}
void target(void* context, XpImageTarget* output) {
  *output = imageWriteTarget(static_cast<Context*>(context)->renderer);
}
void progress(void* context) {
  ImageToFramebufferDecoder::yieldDuringDecode(static_cast<Context*>(context)->lastYieldMs);
}
void diagnostic(void* raw, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  auto& self = *static_cast<Context*>(raw);
  if (event == XP_PNG_DIAG_DecodeStart) {
    self.decodeStart = millis();
    return;
  }
  if (event == XP_PNG_DIAG_DecodeComplete) {
    LOG_DBG("PNG", "PNG decoding complete - render time: %lu ms",
            static_cast<unsigned long>(millis() - self.decodeStart));
    return;
  }
  logPngDiagnostic(event, a, b, c, d, self.config.maxWidth, self.config.maxHeight, self.path);
}
enum class Operation : uint32_t { Open, Write, Close, Abort };
bool write(void* context, uint32_t operation, const uint8_t* bytes, size_t length) {
  auto& self = *static_cast<Context*>(context);
  switch (static_cast<Operation>(operation)) {
    case Operation::Open:
      return Storage.openFileForWrite("IMG", self.config.cachePath, self.cache);
    case Operation::Write:
      if (self.cache.write(bytes, length) == length) return true;
      LOG_ERR("IMG", "Pixel cache write failed (%u bytes)", static_cast<unsigned>(length));
      return false;
    case Operation::Close:
      if (self.cache.close()) return true;
      LOG_ERR("IMG", "Pixel cache close failed");
      return false;
    case Operation::Abort:
      if (self.cache.isOpen()) self.cache.close();
      Storage.remove(self.config.cachePath.c_str());
      return true;
  }
  return false;
}
}  // namespace

bool PngToFramebufferConverter::getDimensionsStatic(const std::string& imagePath, ImageDimensions& out) {
  HalFile file;
  if (!Storage.openFileForRead("PNG", imagePath, file) || file.size() > UINT32_MAX) {
    LOG_ERR("PNG", "Failed to open PNG for dimensions");
    return false;
  }
  uint32_t width = 0, height = 0;
  if (!xp_png_dimensions(&file, readFile, file.size(), &width, &height)) {
    LOG_ERR("PNG", "Failed to open PNG for dimensions");
    return false;
  }
  return validateAndStoreDimensions(static_cast<int32_t>(width), static_cast<int32_t>(height), out, "PNG");
}

bool PngToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                    const RenderConfig& config) {
  LOG_DBG("PNG", "Decoding PNG: %s", imagePath.c_str());
  Context context{{}, {}, renderer, config, imagePath.c_str(), millis()};
  if (!Storage.openFileForRead("PNG", imagePath, context.input) || context.input.size() > UINT32_MAX) {
    LOG_ERR("PNG", "Failed to open PNG");
    return false;
  }
  const XpImageConfig options{config.x,
                              config.y,
                              config.maxWidth,
                              config.maxHeight,
                              renderer.getScreenWidth(),
                              renderer.getScreenHeight(),
                              config.sourceCropX,
                              config.sourceCropY,
                              config.useExactDimensions,
                              config.useDithering,
                              config.preserveAlpha,
                              0};
  const XpPngServices services{&context,  read, target, write, progress, static_cast<uint32_t>(context.input.size()),
                               diagnostic};
  if (!xp_png_render(&services, &options, !config.cachePath.empty())) {
    return false;
  }
  return true;
}

bool PngToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasPngExtension(extension);
}
