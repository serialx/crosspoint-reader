#pragma once

#include <InflateReader.h>
#include <xp_ffi.h>

#include "EpdFontData.h"

class FontDecompressor {
 public:
  static constexpr uint16_t MAX_PAGE_GLYPHS = 512;
  static constexpr uint8_t MAX_PAGE_SLOTS = 8;  // One per font/style pair: body R/B/I/BI plus heading sizes

  FontDecompressor() = default;
  ~FontDecompressor();

  bool init();
  void deinit();

  // Returns pointer to decompressed bitmap data for the given glyph.
  // Checks the page buffer (from prewarm) first, then falls back to the hot group slot.
  const uint8_t* getBitmap(const EpdFontData* fontData, const EpdGlyph* glyph, uint32_t glyphIndex);

  // Free all cached data (page buffer + hot group).
  void clearCache();

  // Pre-scan UTF-8 text and extract needed glyph bitmaps into a flat page buffer.
  // Each group is decompressed once into a temp buffer; only needed glyphs are kept.
  // Returns the number of glyphs that couldn't be loaded (0 on full success).
  int prewarmCache(const EpdFontData* fontData, const char* utf8Text);

  const void* pageSlotView() const { return cache.pageSlots; }
  uint32_t pageSlotViewCount() const { return cache.pageSlotCount; }

  using Stats = XpFontCacheStats;
  void logStats(const char* label = "FDC");
  void resetStats();
  const Stats& getStats() const { return cache.stats; }

 private:
  XpFontCache cache{};
  InflateReader inflateReader;
};
