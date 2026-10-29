#include <FontDecompressor.h>
#include <FontTableGeneration.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static int failAt, allocation;
static std::map<void*, size_t> live;
extern "C" void* fiFontMalloc(size_t size) {
  const bool fail = ++allocation == failAt;
  std::printf("alloc %zu %d\n", size, fail);
  if (fail) return nullptr;
  void* p = std::malloc(size ? size : 1);
  live[p] = size;
  return p;
}
extern "C" void fiFontFree(void* ptr) {
  if (ptr) {
    if (!live.count(ptr)) std::abort();
    std::printf("free %zu\n", live[ptr]);
    live.erase(ptr);
  }
  std::free(ptr);
}

// Raw DEFLATE stored block containing three byte-aligned 2-bit glyphs.
static const uint8_t bitmap[] = {1, 9, 0, 246, 255, 0x1b, 0x40, 0xe4, 0x80, 0xff, 0xaa, 0x55, 0, 0x6c, 0};
static const EpdGlyph glyphs[] = {{3, 2, 48, 0, 2, 2, 0}, {4, 1, 64, 0, 1, 1, 2}, {5, 2, 80, 0, 2, 3, 3}};
static const EpdUnicodeInterval intervals[] = {{65, 67, 0}};
static const EpdFontGroup groups[] = {{0, sizeof(bitmap) - 1, 9, 3, 0}};
static const uint16_t mapping[] = {0, 0, 0};
static const EpdLigaturePair ligatures[] = {{(65 << 16) | 66, 67}};

static void run(int fail, bool mapped, bool broken) {
  std::printf("CASE %d %d %d\n", fail, mapped, broken);
  failAt = fail;
  allocation = 0;
  fontTableGeneration = 0;
  EpdFontData font{};
  font.bitmap = broken ? bitmap + 1 : bitmap;
  font.glyph = glyphs;
  font.intervals = intervals;
  font.intervalCount = 1;
  font.groups = groups;
  font.groupCount = 1;
  font.glyphToGroup = mapped ? mapping : nullptr;
  font.ligaturePairs = ligatures;
  font.ligaturePairCount = 1;
  {
    FontDecompressor cache;
    cache.init();
    std::printf("empty %d\n", cache.prewarmCache(&font, ""));
    for (int phase = 0; phase < 3; ++phase) {
      if (phase == 1) std::printf("prewarm %d\n", cache.prewarmCache(&font, "ABBA?"));
      if (phase == 2) cache.clearCache();
      for (int repeat = 0; repeat < 2; ++repeat) {
        for (uint32_t i = 0; i < 3; ++i) {
          const auto* p = cache.getBitmap(&font, glyphs + i, i);
          std::printf("glyph %u ", i);
          if (p) {
            for (uint16_t j = 0; j < glyphs[i].dataLength; ++j) std::printf("%02x", p[j]);
          } else
            std::printf("null");
          std::putchar('\n');
        }
      }
    }
    for (int i = 0; i < 9; ++i) std::printf("slot %d %d\n", i, cache.prewarmCache(&font, "C"));
    const auto& s = cache.getStats();
    std::printf("stats %u %u %u %u %u %u %u %u %u %u\n", s.cacheHits, s.cacheMisses, s.decompressTimeMs,
                s.uniqueGroupsAccessed, s.pageBufferBytes, s.pageGlyphsBytes, s.hotGroupBytes, s.peakTempBytes,
                s.getBitmapTimeUs, s.getBitmapCalls);
    std::printf("slots %u\n", cache.pageSlotViewCount());
  }
  std::printf("generation %u live %zu\n", fontTableGeneration.load(), live.size());
  if (!live.empty()) std::abort();
}
static void boundary(int glyphCount, int groupCount, bool mapped, int fail) {
  std::printf("BOUNDARY glyphs=%d groups=%d mapped=%d fail=%d\n", glyphCount, groupCount, mapped, fail);
  std::vector<EpdGlyph> glyph(glyphCount);
  std::vector<EpdFontGroup> group(groupCount);
  std::vector<uint16_t> map(glyphCount);
  std::vector<uint8_t> compressed;
  std::string text;
  for (int i = 0; i < glyphCount; ++i) {
    glyph[i] = {4, 1, 64, 0, 1, 1, 0};
    map[i] = mapped ? i % groupCount : i * groupCount / glyphCount;
    const unsigned cp = 0x400 + i;
    text += static_cast<char>(0xc0 | (cp >> 6));
    text += static_cast<char>(0x80 | (cp & 63));
  }
  for (int g = 0; g < groupCount; ++g) {
    std::vector<uint8_t> bytes;
    uint32_t first = 0;
    for (int i = 0; i < glyphCount; ++i) {
      if (map[i] != g) continue;
      if (bytes.empty()) first = i;
      bytes.push_back(static_cast<uint8_t>(i));
    }
    const auto n = static_cast<uint16_t>(bytes.size());
    group[g] = {static_cast<uint32_t>(compressed.size()), static_cast<uint32_t>(n + 5), n, n, first};
    compressed.insert(compressed.end(), {1, static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8),
                                         static_cast<uint8_t>(~n), static_cast<uint8_t>((~n) >> 8)});
    compressed.insert(compressed.end(), bytes.begin(), bytes.end());
  }
  compressed.push_back(0);
  const EpdUnicodeInterval interval{0x400, static_cast<uint32_t>(0x400 + glyphCount - 1), 0};
  EpdFontData font{};
  font.bitmap = compressed.data();
  font.glyph = glyph.data();
  font.intervals = &interval;
  font.intervalCount = 1;
  font.groups = group.data();
  font.groupCount = groupCount;
  font.glyphToGroup = mapped ? map.data() : nullptr;
  allocation = 0;
  failAt = fail;
  fontTableGeneration = 0;
  {
    FontDecompressor cache;
    cache.init();
    for (int phase = 0; phase < 2; ++phase) {
      std::printf("prewarm %d\n", cache.prewarmCache(&font, text.c_str()));
      for (int repeat = 0; repeat < 2; ++repeat) {
        uint32_t hash = 2166136261u, missing = 0;
        for (int i = 0; i < glyphCount; ++i) {
          const auto* p = cache.getBitmap(&font, &glyph[i], i);
          if (!p) ++missing;
          hash = (hash ^ (p ? *p : 256u)) * 16777619u;
        }
        std::printf("pixels %08x missing=%u\n", hash, missing);
      }
      const auto& stats = cache.getStats();
      std::printf("stats %u %u %u %u %u %u %u %u %u %u slots=%u generation=%u\n", stats.cacheHits, stats.cacheMisses,
                  stats.decompressTimeMs, stats.uniqueGroupsAccessed, stats.pageBufferBytes, stats.pageGlyphsBytes,
                  stats.hotGroupBytes, stats.peakTempBytes, stats.getBitmapTimeUs, stats.getBitmapCalls,
                  cache.pageSlotViewCount(), fontTableGeneration.load());
      cache.clearCache();
      // The second prewarm proves recovery after allocation denial and release.
      failAt = 0;
    }
  }
  std::printf("generation %u live %zu\n", fontTableGeneration.load(), live.size());
  if (!live.empty()) std::abort();
}
int main(int argc, char**) {
  if (argc > 1) {
    for (bool mapped : {false, true}) {
      for (int fail : {0, 1, 2, 3, 129, 130, 131, 132, 133}) {
        for (int count : {511, 512, 513}) boundary(count, 1, mapped, fail);
        for (int count : {127, 128, 129}) boundary(129, count, mapped, fail);
        boundary(513, 129, mapped, fail);
      }
    }
    return 0;
  }

  for (bool mapped : {false, true}) {
    for (int fail = 0; fail <= 12; ++fail) run(fail, mapped, false);
    run(0, mapped, true);
  }
}
