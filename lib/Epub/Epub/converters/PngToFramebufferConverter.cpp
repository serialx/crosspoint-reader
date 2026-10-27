#include "PngToFramebufferConverter.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <xp_ffi.h>

#include "ImagePipeline.h"

namespace {
struct Context {
  HalFile input;
  HalFile cache;
  GfxRenderer& renderer;
  const RenderConfig& config;
  uint32_t lastYieldMs = 0;
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
enum class Operation : uint32_t { Open, Write, Close, Abort };
bool write(void* context, uint32_t operation, const uint8_t* bytes, size_t length) {
  auto& self = *static_cast<Context*>(context);
  switch (static_cast<Operation>(operation)) {
    case Operation::Open:
      return Storage.openFileForWrite("PNG", self.config.cachePath, self.cache);
    case Operation::Write:
      return self.cache.write(bytes, length) == length;
    case Operation::Close:
      return self.cache.close();
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
  if (!Storage.openFileForRead("PNG", imagePath, file) || file.size() > UINT32_MAX) return false;
  uint32_t width = 0, height = 0;
  if (!xp_png_dimensions(&file, readFile, file.size(), &width, &height)) {
    LOG_ERR("PNG", "Invalid PNG header");
    return false;
  }
  return validateAndStoreDimensions(width, height, out, "PNG");
}

bool PngToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                    const RenderConfig& config) {
  Context context{{}, {}, renderer, config, millis()};
  if (!Storage.openFileForRead("PNG", imagePath, context.input) || context.input.size() > UINT32_MAX) return false;
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
  const XpPngServices services{&context, read, target, write, progress, static_cast<uint32_t>(context.input.size())};
  if (!xp_png_render(&services, &options, !config.cachePath.empty())) {
    LOG_ERR("PNG", "PNG decode failed: %s", imagePath.c_str());
    return false;
  }
  return true;
}

bool PngToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasPngExtension(extension);
}
