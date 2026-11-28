#include <GfxRenderer.h>

#include <array>
#include <cassert>
#include <cstdio>

int main() {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  const unsigned width = display.getDisplayWidth();
  const unsigned height = display.getDisplayHeight();
  const unsigned stride = display.getDisplayWidthBytes();
  std::array<uint8_t, 8000> scratch;
  for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise, GfxRenderer::PortraitInverted,
                                 GfxRenderer::LandscapeCounterClockwise}) {
    renderer.setOrientation(orientation);
    for (const bool empty : {true, false}) {
      display.pixels.fill(0);
      for (unsigned y = 0; y < height; y += 80) {
        scratch.fill(255);
        renderer.beginStripTarget(scratch.data(), y, 80);
        renderer.copyStatusToStrip(renderer.getScreenHeight() - (empty ? 0 : 1), false);
        renderer.endStripTarget();
        for (unsigned row = 0; row < 80; ++row) {
          for (unsigned x = 0; x < width; ++x) {
            bool status = false;
            switch (orientation) {
              case GfxRenderer::Portrait:
                status = x == width - 1;
                break;
              case GfxRenderer::LandscapeClockwise:
                status = y + row == 0;
                break;
              case GfxRenderer::PortraitInverted:
                status = x == 0;
                break;
              case GfxRenderer::LandscapeCounterClockwise:
                status = y + row == height - 1;
                break;
            }
            const bool white = (scratch[row * stride + x / 8] & (0x80 >> (x & 7))) != 0;
            assert(white == (empty || !status));
          }
        }
      }
    }
  }
  std::puts("PASS empty and last-row status regions in all four orientations");
}
