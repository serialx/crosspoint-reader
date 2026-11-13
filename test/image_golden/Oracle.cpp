#include <GfxRenderer.h>
#include <GifToFramebufferConverter.h>
#include <ImageDimsProbe.h>
#ifndef GIF_ONLY
#include <JpegToFramebufferConverter.h>
#include <PngToFramebufferConverter.h>
#endif

#ifdef READER_IMAGE_ORACLE
#include <ImageDecoderFactory.h>
#include <xp_page_paint.h>
extern "C" uint8_t xp_page_image_test_format(const uint8_t*, size_t);
extern "C" uint8_t xp_page_image_test_dimensions(const uint8_t*, size_t, uint16_t*, uint16_t*);
extern "C" uint8_t xp_page_image_test_decode(const XpPageTarget*, const XpPageImage*, const uint8_t*);
#include <HalStorage.h>

#include <cstring>
#include <new>
extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_live_allocations();
// This oracle observes the unsupported-format probe event; other painter events stay strict.
extern "C" void xp_native_page_image_log(void*, const XpPageImage*, const uint8_t*, uint32_t event, int32_t, int32_t) {
  if (event != 16) std::abort();
}
static std::string faultSource, faultCache, fault;
static size_t faultIndex = 0, faultCalls = 0;
static bool failNativeAllocation = false;
static size_t nativeAllocationIndex = 0;
void* operator new(size_t size, const std::nothrow_t&) noexcept {
  if (failNativeAllocation && nativeAllocationIndex++ == faultIndex) return nullptr;
  return std::malloc(size);
}

static void injectFault(const std::string& source, const std::string& cache) {
  faultSource = source;
  faultCache = cache;
  if (const auto* value = std::getenv("READER_IMAGE_FAULT")) fault = value;
  if (const auto* value = std::getenv("READER_IMAGE_FAULT_INDEX")) faultIndex = std::strtoul(value, nullptr, 10);
  HalFileTest::allowOpen = [](const char* path, const char*) {
    return !((fault == "source-open" && path == faultSource) || (fault == "cache-open" && path == faultCache));
  };
  HalFileTest::allowSeek = [](const char* path, size_t) {
    return !(fault == "seek" && path == faultSource && faultCalls++ == faultIndex);
  };
  HalFileTest::readLimit = [](const char* path, size_t, size_t length) {
    return fault == "read" && path == faultSource && faultCalls++ == faultIndex ? size_t(0) : length;
  };
  HalFileTest::writeLimit = [](const char* path, size_t, size_t length) {
    return fault == "write" && path == faultCache && faultCalls++ == faultIndex ? size_t(0) : length;
  };
  HalFileTest::failNextClose = fault == "close";
  failNativeAllocation = fault == "native-allocation";
  if (fault == "heap") ESP.freeHeap = faultIndex;
  xp_test_fail_allocation_after(fault == "allocation" ? faultIndex : SIZE_MAX);
}
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
#ifdef READER_IMAGE_ORACLE
  if (std::getenv("READER_IMAGE_PROBE_TUPLE")) {
    ImageDimensions dimensions;
    dimensions.width = 0x1234;
    dimensions.height = 0x4321;
    uint32_t format = 0;
    bool valid = false;
    if (std::getenv("READER_IMAGE_LEGACY")) {
      auto* probe = ImageDecoderFactory::getDecoder(path);
      if (dynamic_cast<PngToFramebufferConverter*>(probe)) format = 1;
      if (dynamic_cast<JpegToFramebufferConverter*>(probe)) format = 2;
      if (dynamic_cast<GifToFramebufferConverter*>(probe)) format = 3;
      valid = probe && probe->getDimensions(path, dimensions);
    } else {
      const auto* bytes = reinterpret_cast<const uint8_t*>(path.c_str());
      format = xp_page_image_test_format(bytes, path.size());
      uint16_t width = dimensions.width, height = dimensions.height;
      valid = xp_page_image_test_dimensions(bytes, path.size(), &width, &height) == 2;
      dimensions.width = width;
      dimensions.height = height;
    }
    u32(format);
    u32(valid);
    u32(dimensions.width);
    u32(dimensions.height);
    return xp_test_live_allocations() ? 4 : 0;
  }
#endif
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
#ifdef READER_IMAGE_ORACLE
  const bool probeOnly = std::getenv("READER_IMAGE_PROBE_ONLY");
  if (probeOnly) {
    dims.width = 0x1234;
    dims.height = 0x4321;
    injectFault(path, cache);
  }
  uint16_t width = dims.width, height = dims.height;
  const bool found = std::getenv("READER_IMAGE_LEGACY")
                         ? decoder->getDimensions(path, dims)
                         : xp_page_image_test_dimensions(reinterpret_cast<const uint8_t*>(path.c_str()), path.size(),
                                                         &width, &height) == 2;
  if (!std::getenv("READER_IMAGE_LEGACY")) {
    dims.width = width;
    dims.height = height;
  }
  u32(found);
#else
  u32(decoder->getDimensions(path, dims));
#endif
  u32(dims.width);
  u32(dims.height);
#ifdef READER_IMAGE_ORACLE
  if (probeOnly) {
    if (xp_test_live_allocations()) return 4;
    if (failNativeAllocation && nativeAllocationIndex <= faultIndex) return 5;
    return 0;
  }
#endif
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
#ifdef READER_IMAGE_ORACLE
  if (variant != 2) return 3;
  XpPageTarget targetView{};
  auto& glyph = targetView.glyphs;
  glyph.buffer = renderer.buffer.data();
  glyph.bufferLen = 99 * renderer.rows;
  glyph.width = 792;
  glyph.height = 528;
  glyph.stride = 99;
  glyph.originY = renderer.origin;
  glyph.rows = renderer.rows;
  glyph.orientation = renderer.orientation;
  glyph.plane = renderer.mode;
  glyph.absolute = renderer.absolute;
  targetView.native = &renderer;
  const XpPageImage image{reinterpret_cast<const uint8_t*>(path.c_str()),
                          reinterpret_cast<const uint8_t*>(""),
                          path.size(),
                          0,
                          config.x,
                          config.y,
                          config.maxWidth,
                          config.maxHeight};
  injectFault(path, cache);
  const bool success =
      std::getenv("READER_IMAGE_LEGACY")
          ? decoder->decodeToFramebuffer(path, renderer, config)
          : xp_page_image_test_decode(&targetView, &image, reinterpret_cast<const uint8_t*>(cache.c_str())) == 2;
  if (xp_test_live_allocations()) return 4;
  if (failNativeAllocation && nativeAllocationIndex <= faultIndex) return 5;
  failNativeAllocation = false;
  xp_test_fail_allocation_after(SIZE_MAX);
  u32(success);
#else
  u32(decoder->decodeToFramebuffer(path, renderer, config));
#endif
  blob(renderer.buffer);
  blob(renderer.calls);
  std::ifstream cached(cache, std::ios::binary);
  const bool exists = cached.good();
  std::vector<uint8_t> packed{std::istreambuf_iterator<char>(cached), {}};
  u32(exists);
  blob(packed);
  std::remove(cache.c_str());
}
