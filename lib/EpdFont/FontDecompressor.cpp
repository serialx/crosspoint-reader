#include "FontDecompressor.h"

#include <Arduino.h>
#include <FontAlloc.h>
#include <FontTableGeneration.h>
#include <Logging.h>

struct FontCacheCall {
  InflateReader* inflater;
  const EpdFontData* font;
};

namespace {
XpFontCacheServices services(FontCacheCall* call) {
  return {call,
          [](size_t bytes) { return static_cast<uint8_t*>(fiFontMalloc(bytes)); },
          [](uint8_t* ptr) { fiFontFree(ptr); },
          [](void* ctx, const uint8_t* src, uint32_t size, uint8_t* dst, uint32_t out) -> int32_t {
            auto& reader = *static_cast<FontCacheCall*>(ctx)->inflater;
            if (!reader.init(false)) return -1;
            reader.setSource(src, size);
            return reader.read(dst, out);
          },
          [](bool microseconds) -> uint32_t { return microseconds ? micros() : millis(); },
          [] { ++fontTableGeneration; },
          [](void* ctx, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
            switch (static_cast<XpFontCacheEvent>(event)) {
              case XpFontCacheEvent::Decompression:
                LOG_ERR("FDC", "Decompression failed for group %u", a);
                break;
              case XpFontCacheEvent::MissingGroup:
                LOG_ERR("FDC", "Glyph %u not found in any group", a);
                break;
              case XpFontCacheEvent::HotGroupAllocation:
                LOG_ERR("FDC", "Failed to allocate %u bytes for hot group %u", a, b);
                break;
              case XpFontCacheEvent::ScratchAllocation:
                LOG_ERR("FDC", "Failed to allocate %u bytes for glyph scratch", a);
                break;
              case XpFontCacheEvent::SlotsFull:
                LOG_ERR("FDC", "All %u page buffer slots full, cannot prewarm fontData=%p", a,
                        (void*)static_cast<FontCacheCall*>(ctx)->font);
                break;
              case XpFontCacheEvent::GlyphCap:
                LOG_DBG("FDC", "Glyph cap (%u) reached during prewarm; excess glyphs will use hot-group fallback", a);
                break;
              case XpFontCacheEvent::GroupCap:
                LOG_DBG("FDC", "Group cap (128) reached during prewarm; some groups will use hot-group fallback");
                break;
              case XpFontCacheEvent::PageAllocation:
                LOG_ERR("FDC", "Failed to allocate page buffer (%u bytes, %u glyphs)", a, b);
                break;
              case XpFontCacheEvent::TempAllocation:
                LOG_ERR("FDC", "Failed to allocate temp buffer (%u bytes) for group %u", a, b);
                break;
              case XpFontCacheEvent::Prewarm:
                LOG_DBG("FDC", "Prewarm: %u glyphs in %u bytes from %u groups (%d missed)", a, b, c,
                        static_cast<int>(d));
                break;
            }
          }};
}
}  // namespace

FontDecompressor::~FontDecompressor() { deinit(); }
bool FontDecompressor::init() {
  clearCache();
  return true;
}
void FontDecompressor::deinit() { clearCache(); }
void FontDecompressor::clearCache() {
  const auto io = services(nullptr);
  xp_font_cache_clear(&cache, &io);
}
const uint8_t* FontDecompressor::getBitmap(const EpdFontData* fontData, const EpdGlyph* glyph, uint32_t glyphIndex) {
  FontCacheCall call{&inflateReader, fontData};
  const auto io = services(&call);
  return xp_font_cache_bitmap(&cache, fontData, glyph, glyphIndex, &io);
}
int FontDecompressor::prewarmCache(const EpdFontData* fontData, const char* utf8Text) {
  FontCacheCall call{&inflateReader, fontData};
  const auto io = services(&call);
  return xp_font_cache_prewarm(&cache, fontData, reinterpret_cast<const uint8_t*>(utf8Text),
                               utf8Text ? strlen(utf8Text) : 0, &io);
}
void FontDecompressor::resetStats() { cache.stats = {}; }
void FontDecompressor::logStats(const char* label) {
  const uint32_t total = cache.stats.cacheHits + cache.stats.cacheMisses;
  LOG_DBG("FDC", "[%s] hits=%lu misses=%lu (%.1f%% hit rate)", label, cache.stats.cacheHits, cache.stats.cacheMisses,
          total > 0 ? 100.0f * cache.stats.cacheHits / total : 0.0f);
  LOG_DBG("FDC", "[%s] decompress=%lums groups_accessed=%u", label, cache.stats.decompressTimeMs,
          cache.stats.uniqueGroupsAccessed);
  LOG_DBG("FDC", "[%s] mem: pageBuf=%lu pageGlyphs=%lu hotGroup=%lu peakTemp=%lu", label, cache.stats.pageBufferBytes,
          cache.stats.pageGlyphsBytes, cache.stats.hotGroupBytes, cache.stats.peakTempBytes);
  if (cache.stats.getBitmapCalls > 0) {
    LOG_DBG("FDC", "[%s] getBitmap: %lu calls, %luus total, %luus/call avg", label, cache.stats.getBitmapCalls,
            cache.stats.getBitmapTimeUs, cache.stats.getBitmapTimeUs / cache.stats.getBitmapCalls);
  }
  resetStats();
}
