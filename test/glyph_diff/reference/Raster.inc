// Frozen from 11be2799; independent C++ raster oracle.
enum class TextRotation { None, Rotated90CW };

// Shared glyph rendering logic for normal and rotated text.
// Coordinate mapping and cursor advance direction are selected at compile time via the template parameter.
// Render a glyph scaled by scaleNum/scaleDen: 1/2 for SUP/SUB, 3/4 for small caps.
//
// Each destination pixel samples the source block it covers. At 1/2, any ink in
// the block draws, preserving thin strokes that nearest-neighbor sampling can skip;
// milder reductions need majority coverage so stems don't thicken.
//
// drawText() scales the advance width by the same factor so layout reserves exactly
// the right horizontal space for the scaled glyph.
static void renderCharScaled(const GfxRenderer& renderer, GfxRenderer::RenderMode renderMode,
                             const EpdFontFamily& fontFamily, const uint32_t cp, int cursorX, int cursorY,
                             const bool pixelState, const EpdFontFamily::Style style, const int scaleNum,
                             const int scaleDen, const EpdGlyph* resolvedGlyph = nullptr) {
  if (renderer.grayPlanesAreAbsolute()) renderMode = GfxRenderer::BW;
  EpdGlyph solidFallback;
  const EpdGlyph* glyph = resolvedGlyph ? resolvedGlyph : fontFamily.getGlyphMetrics(cp, solidFallback, style);
  if (!glyph) return;

  const EpdFontData* fontData = fontFamily.getData(style);
  if (glyph == &solidFallback) {
    renderer.fillRect(cursorX + glyph->left * scaleNum / scaleDen, cursorY - glyph->top * scaleNum / scaleDen,
                      (glyph->width * scaleNum + scaleDen - 1) / scaleDen,
                      (glyph->height * scaleNum + scaleDen - 1) / scaleDen, pixelState);
    return;
  }
  const uint8_t* bitmap = renderer.getGlyphBitmap(fontData, glyph);
  if (!bitmap) return;

  const int srcW = glyph->width;
  const int srcH = glyph->height;
  const int dstW = (srcW * scaleNum + scaleDen - 1) / scaleDen;  // ceil so edge columns aren't clipped
  const int dstH = (srcH * scaleNum + scaleDen - 1) / scaleDen;
  // Scale the glyph bearing by the same factor so the scaled glyph sits at the correct
  // pixel offset from the (already-shifted) cursor position.
  const int baseX = cursorX + glyph->left * scaleNum / scaleDen;
  const int baseY = cursorY - glyph->top * scaleNum / scaleDen;
  const bool halving = scaleNum * 2 == scaleDen;
  const bool is2Bit = fontData->is2Bit;

  // Raw sample value: 1-bit fonts read 0 or 3, 2-bit fonts 0 (white) .. 3 (black).
  const auto sample = [&](const int pos) -> uint8_t {
    if (is2Bit) return (bitmap[pos >> 2] >> ((3 - (pos & 3)) * 2)) & 0x3;
    return ((bitmap[pos >> 3] >> (7 - (pos & 7))) & 1) ? 3 : 0;
  };

  for (int dstY = 0; dstY < dstH; dstY++) {
    const int srcY0 = dstY * scaleDen / scaleNum;
    const int srcY1 = std::min(srcH, std::max(srcY0 + 1, ((dstY + 1) * scaleDen + scaleNum - 1) / scaleNum));
    for (int dstX = 0; dstX < dstW; dstX++) {
      const int srcX0 = dstX * scaleDen / scaleNum;
      const int srcX1 = std::min(srcW, std::max(srcX0 + 1, ((dstX + 1) * scaleDen + scaleNum - 1) / scaleNum));
      int coverage = 0;
      uint8_t maxRaw = 0;
      int samples = 0;
      for (int y = srcY0; y < srcY1; y++) {
        for (int x = srcX0; x < srcX1; x++) {
          const uint8_t raw = sample(y * srcW + x);
          coverage += raw;
          maxRaw = std::max(maxRaw, raw);
          samples++;
        }
      }
      const bool ink = halving ? (is2Bit ? (maxRaw >= 2 || coverage >= 2) : maxRaw != 0)
                               : samples > 0 && coverage * 2 >= samples * 3;
      if (ink) renderer.drawPixel(baseX + dstX, baseY + dstY, pixelState);
    }
  }
}

template <TextRotation rotation = TextRotation::None>
static void renderCharImpl(const GfxRenderer& renderer, GfxRenderer::RenderMode renderMode,
                           const EpdFontFamily& fontFamily, const uint32_t cp, int cursorX, int cursorY,
                           const bool pixelState, const EpdFontFamily::Style style,
                           const EpdGlyph* resolvedGlyph = nullptr) {
  if (renderer.grayPlanesAreAbsolute()) renderMode = GfxRenderer::BW;
  EpdGlyph solidFallback;
  const EpdGlyph* glyph = resolvedGlyph ? resolvedGlyph : fontFamily.getGlyphMetrics(cp, solidFallback, style);
  if (!glyph) {
    LOG_ERR("GFX", "No glyph for codepoint %d", cp);
    return;
  }

  const EpdFontData* fontData = fontFamily.getData(style);
  const bool is2Bit = fontData->is2Bit;
  const uint8_t width = glyph->width;
  const uint8_t height = glyph->height;
  const int left = glyph->left;
  const int top = glyph->top;

  // Tiled-grayscale band culling: if this glyph's physical y-extent is entirely
  // outside the active strip, skip it before the expensive bitmap decode. This
  // is what makes per-band re-rendering cheap. No-op outside strip mode.
  if constexpr (rotation == TextRotation::Rotated90CW) {
    const int ob = cursorX + fontData->ascender - top;
    const int ib = cursorY - left;
    if (!renderer.glyphIntersectsStrip(ob, ib - (width - 1), ob + height - 1, ib)) {
      return;
    }
  } else {
    const int gx0 = cursorX + left;
    const int gy0 = cursorY - top;
    if (!renderer.glyphIntersectsStrip(gx0, gy0, gx0 + width - 1, gy0 + height - 1)) {
      return;
    }
  }

  if (glyph == &solidFallback) {
    if constexpr (rotation == TextRotation::Rotated90CW) {
      renderer.fillRect(cursorX + fontData->ascender - top, cursorY - left - width + 1, height, width, pixelState);
    } else {
      renderer.fillRect(cursorX + left, cursorY - top, width, height, pixelState);
    }
    return;
  }
  const uint8_t* bitmap = renderer.getGlyphBitmap(fontData, glyph);
  if (bitmap == nullptr) return;

  // Logical placement of glyph pixel (0, 0) and the logical step for one move
  // along each glyph axis. Rotated text runs glyph x up the screen and glyph
  // y to the right.
  glyphBitmap::Frame frame;
  if constexpr (rotation == TextRotation::Rotated90CW) {
    frame = {cursorX + fontData->ascender - top, cursorY - left, 0, -1, 1, 0};
  } else {
    frame = {cursorX + left, cursorY - top, 1, 0, 0, 1};
  }
  renderer.drawGlyphBitmap(bitmap, width, height, frame, is2Bit, renderMode, pixelState);
}

// Draw an unscaled glyph placed by a logical frame. Equivalent to calling
// drawPixel() for each ink pixel, but the clip test, orientation rotation,
// strip-band check and address math are resolved once per glyph rather than
// once per pixel.
void GfxRenderer::drawGlyphBitmap(const uint8_t* bitmap, const int width, const int height,
                                  const glyphBitmap::Frame& frame, const bool twoBit, const RenderMode mode,
                                  const bool state) const {
  // Apply the logical clip rectangle before rotating, in glyph-local pixels.
  glyphBitmap::Clip clip{0, 0, width, height};
  glyphBitmap::clipToRect(frame, clipLeft_, clipTop_, clipRight_, clipBottom_, clip);

  // Writes go to the framebuffer, or to the strip scratch in tiled grayscale
  // mode; getWriteOriginY()/getWriteRows() bound the rows that exist there.
  glyphBitmap::Target target{getWriteTarget(), panelWidth, panelWidthBytes, getWriteOriginY(), getWriteRows(), {}};
  // Rotate the glyph origin once, then derive the two physical axes by
  // rotating its neighbours along each logical axis. Together these encode
  // the text rotation and the panel orientation as one orthogonal transform.
  glyphBitmap::Frame& physical = target.frame;
  rotateCoordinates(orientation, frame.x, frame.y, &physical.x, &physical.y, panelWidth, panelHeight);
  int nextX, nextY;
  rotateCoordinates(orientation, frame.x + frame.dxX, frame.y + frame.dxY, &nextX, &nextY, panelWidth, panelHeight);
  physical.dxX = nextX - physical.x;
  physical.dxY = nextY - physical.y;
  rotateCoordinates(orientation, frame.x + frame.dyX, frame.y + frame.dyY, &nextX, &nextY, panelWidth, panelHeight);
  physical.dyX = nextX - physical.x;
  physical.dyY = nextY - physical.y;

  const glyphBitmap::Plane plane = mode == BW              ? glyphBitmap::Plane::BW
                                   : mode == GRAYSCALE_MSB ? glyphBitmap::Plane::GrayMSB
                                                           : glyphBitmap::Plane::GrayLSB;
  glyphBitmap::draw(bitmap, width, height, twoBit, plane, state, target, clip);
}

void GfxRenderer::drawScaledGlyph(const int fontId, const uint32_t cp, const EpdFontFamily::Style style, const int x,
                                  const int baselineY, const int scale256) const {
  const auto fontIt = fontMap.find(fontId);
  if (fontIt == fontMap.end()) return;
  const auto& font = fontIt->second;
  const EpdGlyph* glyph = font.getGlyph(cp, style);
  if (!glyph) return;
  const EpdFontData* fontData = font.getData(style);
  const uint8_t* bitmap = getGlyphBitmap(fontData, glyph);
  if (!bitmap) return;

  const int srcW = glyph->width;
  const int srcH = glyph->height;
  const bool is2Bit = fontData->is2Bit;
  // Raw coverage: 1-bit fonts read 0 or 3, 2-bit fonts 0 (white) .. 3 (black); 0 outside the bitmap.
  const auto sample = [&](const int sx, const int sy) -> int {
    if (sx < 0 || sy < 0 || sx >= srcW || sy >= srcH) return 0;
    const int pos = sy * srcW + sx;
    if (is2Bit) return (bitmap[pos >> 2] >> ((3 - (pos & 3)) * 2)) & 0x3;
    return ((bitmap[pos >> 3] >> (7 - (pos & 7))) & 1) ? 3 : 0;
  };

  const int dstW = (srcW * scale256 + 255) / 256;
  const int dstH = (srcH * scale256 + 255) / 256;
  const int left = x + glyph->left * scale256 / 256;
  const int top = baselineY - glyph->top * scale256 / 256;
  // Source position of each destination pixel centre, 16.16 fixed point.
  const int32_t step = (256 << 16) / scale256;
  const int32_t origin = step / 2 - (1 << 15);
  for (int dy = 0; dy < dstH; ++dy) {
    const int32_t sy = origin + dy * step;
    const int y0 = sy >> 16;
    const int fy = (sy >> 8) & 0xFF;
    for (int dx = 0; dx < dstW; ++dx) {
      const int32_t sx = origin + dx * step;
      const int x0 = sx >> 16;
      const int fx = (sx >> 8) & 0xFF;
      const int upper = sample(x0, y0) * (256 - fx) + sample(x0 + 1, y0) * fx;
      const int lower = sample(x0, y0 + 1) * (256 - fx) + sample(x0 + 1, y0 + 1) * fx;
      // Ink where interpolated coverage reaches half of full black (3).
      if ((upper * (256 - fy) + lower * fy) * 2 >= 3 * 256 * 256) drawPixel(left + dx, top + dy, true);
    }
  }
}
