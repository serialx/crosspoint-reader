#pragma once
#include <GfxRenderer.h>
#include <xp_ffi.h>
#include <xp_page_paint.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <string>
extern "C" void xp_page_image_test_paint(const XpPageTarget*, const XpPageImage*);
extern "C" bool xp_page_image_test_cache(const XpPageImage*, bool);

// Preserve the historical fixture's rectangle trace after checking the shipping pixels.
class ReaderImage {
  std::string path, source;
  int width, height;
  XpPageImage image(int x = 0, int y = 0) const {
    return {reinterpret_cast<const uint8_t*>(path.data()),
            reinterpret_cast<const uint8_t*>(source.data()),
            path.size(),
            source.size(),
            x,
            y,
            width,
            height};
  }

 public:
  ReaderImage(const std::string& path, const std::string& source, int width, int height)
      : path(path), source(source), width(width), height(height) {}
  bool hasValidCache() const {
    auto i = image();
    return xp_page_image_test_cache(&i, false);
  }
  bool needsDecode() const {
    auto i = image();
    return xp_page_image_test_cache(&i, true);
  }
  static void clearRenderFailures() { xp_assets_image_reset(false); }
  static void releaseRenderCache() { xp_assets_image_reset(true); }
  void render(GfxRenderer& renderer, int x, int y) const {
    XpPageTarget target{};
    auto& g = target.glyphs;
    g.buffer = renderer.buffer.data();
    g.bufferLen = renderer.buffer.size();
    g.width = 792;
    g.height = 528;
    g.stride = 99;
    g.rows = 528;
    g.orientation = renderer.orientation;
    g.plane = renderer.mode;
    g.absolute = renderer.absolute;
    g.scanning = renderer.fonts.scanning;
    g.clip = {0, 0, (g.orientation & 1) ? 792 : 528, (g.orientation & 1) ? 528 : 792};
    target.native = &renderer;
    auto i = image(x, y);
    std::array<uint8_t, 792 * 528 / 8> before, expected;
    std::copy(renderer.buffer.begin(), renderer.buffer.end(), before.begin());
    xp_page_image_test_paint(&target, &i);
    if (g.scanning || x < 0 || y < 0 || x + width > g.clip.right || y + height > g.clip.bottom ||
        !xp_assets_image_failed(i.path, i.path_len))
      return;
    expected = before;
    // Independent per-pixel reference for the placeholder's black border and white interior.
    for (int dy = 0; dy < height; ++dy)
      for (int dx = 0; dx < width; ++dx) {
        int lx = x + dx, ly = y + dy, px, py;
        switch (g.orientation) {
          case 0:
            px = ly;
            py = 527 - lx;
            break;
          case 1:
            px = 791 - lx;
            py = 527 - ly;
            break;
          case 2:
            px = 791 - ly;
            py = lx;
            break;
          default:
            px = lx;
            py = ly;
            break;
        }
        auto& byte = expected[py * 99 + px / 8];
        const uint8_t mask = 0x80 >> (px % 8);
        if (dx == 0 || dy == 0 || dx == width - 1 || dy == height - 1)
          byte &= ~mask;
        else
          byte |= mask;
      }
    assert(std::equal(expected.begin(), expected.end(), renderer.buffer.begin()));
    std::copy(before.begin(), before.end(), renderer.buffer.begin());
    renderer.fillRect(x, y, width, height, true);
    renderer.fillRect(x + 1, y + 1, width - 2, height - 2, false);
  }
};
