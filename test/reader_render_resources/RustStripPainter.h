#pragma once
#include <Epub/Page.h>
#include <GfxRenderer.h>

// Same physical fixture inputs as ReaderStripPainter; replay runs in Rust.
struct RustStripPainter {
  GfxRenderer& renderer;
  const Page& page;
  int font, left, top, statusTop;
  bool inverted;
  void* highlightsContext = nullptr;
  void (*highlights)(void*, const Page&, int, int, int) = nullptr;
  uint16_t committedRows = 0;
  static uint32_t paint(void* raw, uint8_t* scratch, uint16_t y, uint16_t rows);
};
