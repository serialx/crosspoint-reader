#include <GfxRenderer.h>
#include <GifToFramebufferConverter.h>
#include <ImageDimsProbe.h>
#ifndef GIF_ONLY
#include <JpegToFramebufferConverter.h>
#include <PngToFramebufferConverter.h>
#endif

#include <fstream>
#include <iterator>
#include <vector>

void u32(uint32_t value) {
  for (int i = 0; i < 4; ++i) std::putchar((value >> (i * 8)) & 255);
}
void blob(const std::vector<uint8_t>& data) {
  u32(data.size());
  std::fwrite(data.data(), 1, data.size(), stdout);
}

int main(int argc, char** argv) {
  if (argc != 5) return 2;
  const std::string path = argv[1], cache = argv[2];
  const int variant = std::atoi(argv[3]), target = std::atoi(argv[4]);
  GfxRenderer renderer;
  renderer.orientation = static_cast<GfxRenderer::Orientation>(target % 4);
  renderer.mode = static_cast<GfxRenderer::RenderMode>((target / 4) % 3);
  renderer.absolute = (target / 12) % 2;
  if (target / 24) {
    renderer.origin = 197;
    renderer.rows = 41;
  }
  RenderConfig config{};
  config.x = 5;
  config.y = 7;
  config.maxWidth = 137;
  config.maxHeight = 173;
  config.useDithering = variant % 2 == 0;
  config.useGrayscale = variant % 3 == 0;
  config.performanceMode = variant % 4 == 0;
  config.useExactDimensions = variant >= 2;
  config.preserveAlpha = variant >= 6;
  config.sourceCropX = variant >= 4 ? 0.31f : 0.f;
  config.sourceCropY = variant >= 4 ? 0.47f : 0.f;
  if (variant == 8) {
    config.maxWidth = 11;
    config.maxHeight = 9;
  }
  if (variant == 9) {
    config.maxWidth = 301;
    config.maxHeight = 277;
  }
  if (variant == 10) {
    config.x = renderer.getScreenWidth() - 19;
    config.y = renderer.getScreenHeight() - 13;
  }
  if (variant >= 11) {
    config.useExactDimensions = false;
    config.preserveAlpha = variant == 11;
    config.sourceCropX = variant == 12 ? 0.8f : 0.f;
    config.sourceCropY = variant == 13 ? 0.9f : 0.f;
  }
  if (variant != 3) config.cachePath = cache;
  std::remove(cache.c_str());
#ifndef GIF_ONLY
  PngToFramebufferConverter png;
  JpegToFramebufferConverter jpeg;
#endif
  GifToFramebufferConverter gif;
#ifdef GIF_ONLY
  ImageToFramebufferDecoder* decoder = &gif;
#else
  ImageToFramebufferDecoder* decoder = path.ends_with(".png")   ? static_cast<ImageToFramebufferDecoder*>(&png)
                                       : path.ends_with(".gif") ? static_cast<ImageToFramebufferDecoder*>(&gif)
                                                                : static_cast<ImageToFramebufferDecoder*>(&jpeg);
#endif
  ImageDimensions dims{};
  u32(decoder->getDimensions(path, dims));
  u32(dims.width);
  u32(dims.height);
  std::ifstream input(path, std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
  for (size_t chunk : {size_t(1), size_t(7), size_t(4096)}) {
    ImageDimsProbe probe;
    size_t accepted = 0;
    for (size_t i = 0; i < bytes.size(); i += chunk) {
      const size_t n = std::min(chunk, bytes.size() - i), wrote = probe.write(bytes.data() + i, n);
      accepted += wrote;
      if (wrote != n) break;
    }
    dims = {};
    u32(probe.getDimensions(dims));
    u32(dims.width);
    u32(dims.height);
    u32(accepted);
  }
  u32(decoder->decodeToFramebuffer(path, renderer, config));
  blob(renderer.buffer);
  blob(renderer.calls);
  std::ifstream cached(cache, std::ios::binary);
  const bool exists = cached.good();
  std::vector<uint8_t> packed{std::istreambuf_iterator<char>(cached), {}};
  u32(exists);
  blob(packed);
  std::remove(cache.c_str());
}
