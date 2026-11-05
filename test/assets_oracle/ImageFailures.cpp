#include <GfxRenderer.h>
#include <HalStorage.h>
#include <blocks/ImageBlock.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

extern "C" void asset_fail_platform_growth(size_t skip, size_t count);
extern "C" size_t asset_failed_platform_growths();
extern "C" size_t asset_platform_attempts();
extern "C" size_t asset_platform_live();

namespace {
enum class Fault { None, ReadOnly, DiskFull, Read };
Fault fault = Fault::None;
size_t faultHits = 0, sourceOpens = 0;
bool isCache(const char* path) { return std::string_view(path).ends_with(".px2"); }
bool allowOpen(const char* path, const char* mode) {
  if (!isCache(path) && mode[0] == 'r') ++sourceOpens;
  if (isCache(path) && mode[0] == 'w' && fault == Fault::ReadOnly) {
    ++faultHits;
    return false;
  }
  return true;
}
size_t readLimit(const char* path, size_t offset, size_t count) {
  if (isCache(path) && offset >= 4 && fault == Fault::Read) {
    ++faultHits;
    return 0;
  }
  return count;
}
size_t writeLimit(const char* path, size_t offset, size_t count) {
  // Permit the header and part of the first payload write, then simulate ENOSPC.
  if (isCache(path) && fault == Fault::DiskFull && offset + count > 7) {
    ++faultHits;
    return offset < 7 ? 7 - offset : 0;
  }
  return count;
}
void reset() {
  ImageBlock::clearRenderFailures();
  ImageBlock::releaseRenderCache();
  assert(asset_platform_live() == 0);
  fault = Fault::None;
  faultHits = 0;
  sourceOpens = 0;
  asset_fail_platform_growth(0, 0);
}
void plane(GfxRenderer& renderer, int orientation, int mode) {
  renderer.orientation = static_cast<GfxRenderer::Orientation>(orientation);
  renderer.mode = static_cast<GfxRenderer::RenderMode>(mode % 3);
  renderer.absolute = mode >= 3;
  std::fill(renderer.buffer.begin(), renderer.buffer.end(), 0xa5);
  renderer.rectangles.clear();
}
void cacheFile(const std::string& path, int width, int height) {
  std::vector<uint8_t> bytes(4 + ((width + 3) / 4) * height);
  bytes[0] = width;
  bytes[1] = width >> 8;
  bytes[2] = height;
  bytes[3] = height >> 8;
  for (size_t i = 4; i < bytes.size(); ++i) bytes[i] = static_cast<uint8_t>(i * 71 + (i >> 6));
  auto* file = std::fopen(path.c_str(), "wb");
  assert(file);
  assert(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
  assert(std::fclose(file) == 0);
}
void placeholder(const GfxRenderer& renderer, int x, int y, int w, int h) {
  const std::vector<int> expected{x, y, w, h, 1, x + 1, y + 1, w - 2, h - 2, 0};
  assert(renderer.rectangles == expected);
}
void mixedPage(const std::string& root) {
  std::array<std::string, 3> stems{root + "/a", root + "/" + std::string(60, 'b'), root + "/" + std::string(130, 'c')};
  constexpr std::array<int, 3> widths{1, 401, 173}, heights{1, 17, 23}, ys{5, 20, 50};
  for (int i = 0; i < 3; ++i) cacheFile(stems[i] + ".px2", widths[i], heights[i]);
  std::array<ImageBlock, 3> images{ImageBlock(stems[0] + ".jpg", "", widths[0], heights[0]),
                                   ImageBlock(stems[1] + ".jpg", "", widths[1], heights[1]),
                                   ImageBlock(stems[2] + ".jpg", "", widths[2], heights[2])};
  ESP.maxAllocHeap = 0;  // Reject the RAM slot, retain enough heap for real decoder fallback.
  for (int orientation = 0; orientation < 4; ++orientation) {
    std::array<std::vector<uint8_t>, 6> expected;
    for (int cycle = 0; cycle < 3; ++cycle) {
      reset();
      GfxRenderer renderer;
      const size_t failedBefore = asset_failed_platform_growths();
      size_t warmAttempts = 0;
      for (int pass = 0; pass < 18; ++pass) {
        const int mode = pass % 6;
        plane(renderer, orientation, mode);
        for (int i = 0; i < 3; ++i) {
          // First grow the longer path, then fail the larger row window. Its
          // one-row fallback must preserve the path and yield identical pixels.
          if (cycle == 1 && pass == 0 && i == 1) asset_fail_platform_growth(1, 1);
          images[i].render(renderer, 7, ys[i]);
        }
        assert(renderer.rectangles.empty());
        if (cycle == 0 && pass < 6) expected[mode] = renderer.buffer;
        assert(renderer.buffer == expected[mode]);
        if (pass == 0)
          warmAttempts = asset_platform_attempts();
        else
          assert(asset_platform_attempts() == warmAttempts);
      }
      assert(asset_failed_platform_growths() == failedBefore + (cycle == 1 ? 1 : 0));
    }
    reset();
    GfxRenderer renderer;
    images[0].render(renderer, 7, ys[0]);
    const size_t failedBefore = asset_failed_platform_growths();
    asset_fail_platform_growth(1, 2);  // Both full-window and one-row growth fail.
    for (int mode = 0; mode < 6; ++mode) {
      plane(renderer, orientation, mode);
      images[1].render(renderer, 7, ys[1]);
      placeholder(renderer, 7, ys[1], widths[1], heights[1]);
    }
    assert(asset_failed_platform_growths() == failedBefore + 2);
    reset();
    for (int mode = 0; mode < 6; ++mode) {
      plane(renderer, orientation, mode);
      for (int i = 0; i < 3; ++i) images[i].render(renderer, 7, ys[i]);
      assert(renderer.rectangles.empty() && renderer.buffer == expected[mode]);
    }
  }
  reset();
}
void decoderFaults(const std::string& path) {
  const auto cache = std::filesystem::path(path).replace_extension(".px2");
  const auto backup = path + ".saved";
  std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing);
  ImageBlock image(path, "", 37, 29);
  ESP.maxAllocHeap = 0;
  for (int orientation = 0; orientation < 4; ++orientation) {
    for (int mode = 0; mode < 6; ++mode) {
      reset();
      std::filesystem::remove(cache);
      GfxRenderer renderer;
      plane(renderer, orientation, mode);
      image.render(renderer, 11, 17);
      assert(renderer.rectangles.empty() && std::filesystem::exists(cache));
      const auto expected = renderer.buffer;
      assert(std::any_of(expected.begin(), expected.end(), [](uint8_t b) { return b != 0xa5; }));
      for (Fault injected : {Fault::ReadOnly, Fault::DiskFull, Fault::Read}) {
        reset();
        if (injected != Fault::Read) std::filesystem::remove(cache);
        fault = injected;
        plane(renderer, orientation, mode);
        image.render(renderer, 11, 17);
        assert(faultHits > 0 && sourceOpens > 0);
        assert(renderer.rectangles.empty() && renderer.buffer == expected);
        if (injected != Fault::Read) assert(!std::filesystem::exists(cache));
        // On the next render, cleared faults allow creation of a usable cache.
        fault = Fault::None;
        plane(renderer, orientation, mode);
        image.render(renderer, 11, 17);
        assert(renderer.rectangles.empty() && renderer.buffer == expected);
        assert(std::filesystem::exists(cache));
      }
      // A failed cache read plus a malformed source reaches the actual decoder,
      // draws the existing placeholder, and suppresses repeated work this render.
      reset();
      fault = Fault::Read;
      auto* file = std::fopen(path.c_str(), "wb");
      assert(file && std::fputc(1, file) != EOF && std::fclose(file) == 0);
      plane(renderer, orientation, mode);
      image.render(renderer, 11, 17);
      assert(faultHits > 0 && sourceOpens > 0);
      placeholder(renderer, 11, 17, 37, 29);
      const size_t opens = sourceOpens;
      plane(renderer, orientation, mode);
      image.render(renderer, 11, 17);
      placeholder(renderer, 11, 17, 37, 29);
      assert(sourceOpens == opens);
      std::filesystem::copy_file(backup, path, std::filesystem::copy_options::overwrite_existing);
      std::filesystem::remove(cache);
      fault = Fault::None;
      ImageBlock::clearRenderFailures();
      plane(renderer, orientation, mode);
      image.render(renderer, 11, 17);
      assert(sourceOpens > opens && renderer.rectangles.empty() && renderer.buffer == expected);
    }
  }
  reset();
  std::filesystem::remove(backup);
}
}  // namespace
int main(int argc, char** argv) {
  assert(argc == 2);
  HalFileTest::allowOpen = allowOpen;
  HalFileTest::readLimit = readLimit;
  HalFileTest::writeLimit = writeLimit;
  mixedPage(argv[1]);
  decoderFaults(std::string(argv[1]) + "/source.jpg");
  decoderFaults(std::string(argv[1]) + "/source.png");
  std::puts("mixed streaming/growth/release/reopen and real JPEG/PNG cache I/O failures passed");
}
