#include "GifToFramebufferConverter.h"

#include <FsHelpers.h>
#include <GifCommon.h>
#include <Logging.h>

#include "ImagePipeline.h"

namespace {
struct Target {
  GfxRenderer& renderer;
  const RenderConfig& config;
  HalFile cache;
  static void target(void* context, XpImageTarget* out) {
    *out = imageWriteTarget(static_cast<Target*>(context)->renderer);
  }
  enum class Operation : uint32_t { Open, Write, Close, Abort };
  static bool io(void* context, uint32_t operation, const uint8_t* bytes, size_t len) {
    auto& self = *static_cast<Target*>(context);
    switch (static_cast<Operation>(operation)) {
      case Operation::Open:
        return Storage.openFileForWrite("GIF", self.config.cachePath, self.cache);
      case Operation::Write:
        return self.cache.write(bytes, len) == len;
      case Operation::Close:
        return self.cache.close();
      case Operation::Abort:
        if (self.cache.isOpen()) self.cache.close();
        Storage.remove(self.config.cachePath.c_str());
        return true;
    }
    return false;
  }
};
}  // namespace

bool GifToFramebufferConverter::getDimensionsStatic(const std::string& path, ImageDimensions& out) {
  HalFile file;
  GifBasicInfo info;
  if (!Storage.openFileForRead("GIF", path, file) || !GifCommon::readBasicInfo(file, info)) {
    LOG_ERR("GIF", "Failed to inspect GIF: %s", path.c_str());
    return false;
  }
  return validateAndStoreDimensions(info.canvasWidth, info.canvasHeight, out, "GIF");
}

bool GifToFramebufferConverter::decodeToFramebuffer(const std::string& path, GfxRenderer& renderer,
                                                    const RenderConfig& config) {
  HalFile file;
  if (!Storage.openFileForRead("GIF", path, file)) return false;
  GifCommon::Source source{file};
  const auto input = source.input();
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
                              2};
  Target target{renderer, config, {}};
  const XpImageCallbacks sink{&target, Target::target, Target::io};
  if (xp_gif_frame(&input, &options, &sink, !config.cachePath.empty())) return true;
  LOG_ERR("GIF", "GIF decode failed (input, allocation, or output): %s", path.c_str());
  return false;
}

bool GifToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasGifExtension(extension);
}
