#pragma once

#include <Epub/Page.h>
#include <GfxRenderer.h>

#include <cstring>

// The page, renderer and framebuffer stay exclusively borrowed through all panel replays.
struct ReaderStripPainter {
  GfxRenderer& renderer;
  const Page& page;
  int font, left, top, statusTop;
  bool inverted;
  void* highlightsContext = nullptr;
  void (*highlights)(void*, const Page&, int, int, int) = nullptr;
  uint16_t committedRows = 0;

  static uint32_t paint(void* raw, uint8_t* scratch, uint16_t y, uint16_t rows) {
    auto& c = *static_cast<ReaderStripPainter*>(raw);
    auto& r = c.renderer;
    const size_t bytes = static_cast<size_t>(rows) * r.getDisplayWidthBytes();
    auto* retained = r.getFrameBuffer() + static_cast<size_t>(y) * r.getDisplayWidthBytes();
    if (!c.page.hasImages() && y < c.committedRows) {
      if (y + rows > c.committedRows) return 1;
      // Later font misses must not change bytes already committed to the panel.
      memcpy(scratch, retained, bytes);
      return 0;
    }
    if (!c.page.hasImages() && y != c.committedRows) return 1;
    const auto priorMode = r.getRenderMode();
    r.setRenderMode(GfxRenderer::BW);
    r.beginStripTarget(scratch, y, rows);
    r.clearScreen();
    if (c.highlights) c.highlights(c.highlightsContext, c.page, c.font, c.left, c.top);
    c.page.render(r, c.font, c.left, c.top);
    r.copyStatusToStrip(c.statusTop, c.inverted);
    r.endStripTarget();
    if (c.inverted) {
      for (size_t i = 0; i < bytes; ++i) scratch[i] ^= 0xFF;
    }
    r.setRenderMode(priorMode);
    if (c.page.hasImages()) {
      return memcmp(scratch, retained, bytes) == 0 ? 0 : 1;
    }
    memcpy(retained, scratch, bytes);
    c.committedRows += rows;
    return 0;
  }
};
