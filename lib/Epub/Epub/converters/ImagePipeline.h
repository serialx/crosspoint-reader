#pragma once

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <xp_ffi.h>

#include "ImageDiagnostics.h"
#include "ImageToFramebufferDecoder.h"

static_assert(GfxRenderer::BW == 0 && GfxRenderer::GRAYSCALE_LSB == 1 && GfxRenderer::GRAYSCALE_MSB == 2,
              "Render modes must match XpImageTarget");
static_assert(GfxRenderer::Portrait == 0 && GfxRenderer::LandscapeClockwise == 1 &&
                  GfxRenderer::PortraitInverted == 2 && GfxRenderer::LandscapeCounterClockwise == 3,
              "Orientations must match XpImageTarget");

inline XpImageTarget imageWriteTarget(GfxRenderer& renderer) {
  XpImageTarget target{};
  target.data = renderer.getWriteTarget();
  target.origin = renderer.getWriteOriginY();
  target.rows = renderer.getWriteRows();
  target.mode = renderer.getRenderMode();
  target.absolute = renderer.grayPlanesAreAbsolute();
  target.stride = renderer.getDisplayWidthBytes();
  target.width = renderer.getDisplayWidth();
  target.height = renderer.getDisplayHeight();
  target.orientation = renderer.getOrientation();
  target.len = static_cast<size_t>(target.stride) * target.rows;
  return target;
}

// Owns HalFile and lends renderer targets to the synchronous Rust row pipeline.
class ImagePipeline {
 public:
  enum class Kind : uint8_t { Png, Jpeg, Gif };
  ImagePipeline(GfxRenderer& renderer, const RenderConfig& config) : renderer(renderer), config(config) {}
  ~ImagePipeline() {
    if (live) xp_image_destroy(&storage);
  }
  ImagePipeline(const ImagePipeline&) = delete;
  ImagePipeline& operator=(const ImagePipeline&) = delete;

  bool init(int width, int height, Kind kind, bool progressive = false, const char* imagePath = "") {
    path = imagePath;
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
                                static_cast<uint8_t>(kind)};
    const XpImageCallbacks callbacks{this, target, io, diagnostic};
    live = xp_image_init(&storage, &options, width, height, progressive, &callbacks, &plan);
    return live;
  }
  bool prepare() { return xp_image_prepare(&storage); }
  bool beginCache() { return xp_image_cache_begin(&storage); }
  bool row(const XpImageRow& row) { return xp_image_row(&storage, &row); }
  bool finish(bool& success) { return xp_image_finish(&storage, &success); }
  XpImagePlan plan{};

 private:
  static void diagnostic(void* raw, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    auto& self = *static_cast<ImagePipeline*>(raw);
    logImageDiagnostic(event, a, b, c, d, self.config.cachePath.c_str(), self.path);
  }
  static void target(void* context, XpImageTarget* out) {
    *out = imageWriteTarget(static_cast<ImagePipeline*>(context)->renderer);
  }
  enum class Operation : uint32_t { Open, Write, Close, Abort };
  static bool io(void* context, uint32_t operation, const uint8_t* bytes, size_t len) {
    auto& self = *static_cast<ImagePipeline*>(context);
    switch (static_cast<Operation>(operation)) {
      case Operation::Open:
        return Storage.openFileForWrite("IMG", self.config.cachePath, self.file);
      case Operation::Write:
        return self.file.write(bytes, len) == len;
      case Operation::Close:
        return self.file.close();
      case Operation::Abort:
        if (self.file.isOpen()) self.file.close();
        Storage.remove(self.config.cachePath.c_str());
        return true;
    }
    return false;
  }
  XpImageStorage storage;
  GfxRenderer& renderer;
  const RenderConfig& config;
  HalFile file;
  bool live = false;
  const char* path = "";
};
