#pragma once
#include <xp_ffi.h>
extern "C" {
#ifdef __cplusplus
enum class XpFontCacheEvent : uint32_t {
  Decompression = 1,
  MissingGroup = 2,
  HotGroupAllocation = 3,
  ScratchAllocation = 4,
  SlotsFull = 5,
  GlyphCap = 6,
  GroupCap = 7,
  PageAllocation = 8,
  TempAllocation = 9,
  Prewarm = 10
};
#endif
// Built-in cache state is inline in FontDecompressor; no extra owner allocation.
// Fonts and glyph indices are validated resident tables. Callbacks are synchronous,
// cannot reenter the cache, and cannot retain any borrowed pointer.
#ifdef __cplusplus
#define XP_FONT_STAT_ZERO = 0
#else
#define XP_FONT_STAT_ZERO
#endif
typedef struct XpFontCacheStats {
  uint32_t cacheHits XP_FONT_STAT_ZERO;
  uint32_t cacheMisses XP_FONT_STAT_ZERO;
  uint32_t decompressTimeMs XP_FONT_STAT_ZERO;
  uint16_t uniqueGroupsAccessed XP_FONT_STAT_ZERO;
  uint32_t pageBufferBytes XP_FONT_STAT_ZERO;
  uint32_t pageGlyphsBytes XP_FONT_STAT_ZERO;
  uint32_t hotGroupBytes XP_FONT_STAT_ZERO;
  uint32_t peakTempBytes XP_FONT_STAT_ZERO;
  uint32_t getBitmapTimeUs XP_FONT_STAT_ZERO;
  uint32_t getBitmapCalls XP_FONT_STAT_ZERO;
} XpFontCacheStats;
#undef XP_FONT_STAT_ZERO
typedef struct {
  uint32_t glyphIndex, bufferOffset, alignedOffset;
} XpFontCacheEntry;
typedef struct {
  uint8_t* buffer;
  const void* fontData;
  XpFontCacheEntry* glyphs;
  uint16_t glyphCount;
} XpFontCacheSlot;
typedef struct {
  XpFontCacheStats stats;
  XpFontCacheSlot pageSlots[8];
  uint8_t pageSlotCount;
  const void* hotGroupFont;
  uint16_t hotGroupIndex;
  uint8_t* hotGroup;
  uint32_t hotGroupCapacity;
  uint8_t* hotGlyphBuf;
  uint32_t hotGlyphBufCapacity;
} XpFontCache;
typedef struct {
  void* ctx;
  uint8_t* (*alloc)(size_t);
  void (*free)(uint8_t*);
  // -1: initialization failed; 0: decoding failed; 1: exact output produced.
  int32_t (*inflate)(void*, const uint8_t*, uint32_t, uint8_t*, uint32_t);
  uint32_t (*clock)(bool microseconds);
  void (*generation)();
  void (*diagnostic)(void*, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
} XpFontCacheServices;
void xp_font_cache_clear(XpFontCache*, const XpFontCacheServices*);
const uint8_t* xp_font_cache_bitmap(XpFontCache*, const void* font, const void* glyph, uint32_t index,
                                    const XpFontCacheServices*);
int32_t xp_font_cache_prewarm(XpFontCache*, const void* font, const uint8_t* text, size_t length,
                              const XpFontCacheServices*);

void xp_font_test_init(void*);
void xp_font_test_clear(void*, const XpFontCacheServices*);
const uint8_t* xp_font_test_bitmap(void*, const void*, const void*, uint32_t, const XpFontCacheServices*);
int32_t xp_font_test_prewarm(void*, const void*, const char*, const XpFontCacheServices*);
const XpFontCache* xp_font_test_view(void*);
void xp_font_test_reset_stats(const void*);
void* xp_font_test_owner_cache(const void*);
const uint8_t* xp_font_test_owner_bitmap(const void*, const void*, const void*);
}
