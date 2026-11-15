#pragma once

#include <xp_book.h>

#include <cstdint>

#include "FootnoteEntry.h"

// One laid-out internal EPUB link. Coordinates are relative to the page origin;
// the reader adds its oriented margins when hit-testing the displayed page.
struct PageLink {
  char href[FOOTNOTE_HREF_LEN];
  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 0;
  int16_t height = 0;

  PageLink() : href{} {}

  bool contains(const int pageX, const int pageY, const int slop, const int minWidth) const {
    const int16_t bounds[4] = {x, y, width, height};
    return xp_book_link_contains(bounds, pageX, pageY, slop, minWidth);
  }
};
