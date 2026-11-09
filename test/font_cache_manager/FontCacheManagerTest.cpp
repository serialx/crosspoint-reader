#include <gtest/gtest.h>

#include <cstdlib>
#include <map>
#include <new>

#include "FontCacheManager.h"
#include "FontDecompressor.h"
#include "SdCardFont.h"

namespace {

bool countHeapAllocations = false;
size_t heapAllocationCount = 0;

// TTF fonts are PSRAM-gated and stubbed out on the host (see stubs/TtfEpdFont.h);
// every manager gets an empty map for that slot.
const std::map<int, TtfEpdFont*> kNoTtfFonts;

const SdCardFont::PrewarmCall* findCall(const SdCardFont& font, const uint8_t styleMask) {
  for (int i = 0; i < font.prewarmCallCount; i++) {
    if (font.prewarmCalls[i].styleMask == styleMask) return &font.prewarmCalls[i];
  }
  return nullptr;
}

}  // namespace

void* operator new(const size_t size) {
  if (countHeapAllocations) heapAllocationCount++;
  if (void* allocation = std::malloc(size)) return allocation;
  throw std::bad_alloc();
}

void operator delete(void* allocation) noexcept { std::free(allocation); }

void operator delete(void* allocation, size_t) noexcept { std::free(allocation); }

TEST(FontCacheManagerTest, PrewarmScopeBatchesEachFontAndResolvedStyleSeparately) {
  SdCardFont readerFont;
  SdCardFont fallbackFont;
  FontRegistry noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{-17, &readerFont}, {23, &fallbackFont}};
  noBuiltinFonts.attach(sdFonts);
  FontCacheManager manager(noBuiltinFonts, sdFonts, kNoTtfFonts);

  auto scope = manager.createPrewarmScope();
  manager.recordText("A", -17, EpdFontFamily::REGULAR);
  manager.recordText("B", -17, EpdFontFamily::BOLD);
  manager.recordText("C", 23, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();

  ASSERT_EQ(2, readerFont.prewarmCallCount);
  const auto* regularCall = findCall(readerFont, 0x01);
  ASSERT_NE(nullptr, regularCall);
  EXPECT_STREQ("A", regularCall->text);
  EXPECT_FALSE(regularCall->accumulate);
  const auto* boldCall = findCall(readerFont, 0x02);
  ASSERT_NE(nullptr, boldCall);
  EXPECT_STREQ("B", boldCall->text);
  EXPECT_FALSE(boldCall->accumulate);

  ASSERT_EQ(1, fallbackFont.prewarmCallCount);
  EXPECT_STREQ("C", fallbackFont.prewarmCalls[0].text);
  EXPECT_EQ(0x01, fallbackFont.prewarmCalls[0].styleMask);
}

TEST(FontCacheManagerTest, PrewarmScopeMergesStylesThatResolveToTheSameSdFontData) {
  SdCardFont font;
  font.resolvedStyles[EpdFontFamily::BOLD] = EpdFontFamily::REGULAR;
  FontRegistry noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{7, &font}};
  noBuiltinFonts.attach(sdFonts);
  FontCacheManager manager(noBuiltinFonts, sdFonts, kNoTtfFonts);

  auto scope = manager.createPrewarmScope();
  manager.recordText("A", 7, EpdFontFamily::REGULAR);
  manager.recordText("B", 7, EpdFontFamily::BOLD);
  scope.endScanAndPrewarm();

  ASSERT_EQ(1, font.prewarmCallCount);
  EXPECT_STREQ("AB", font.prewarmCalls[0].text);
  EXPECT_EQ(0x01, font.prewarmCalls[0].styleMask);
}

TEST(FontCacheManagerTest, PrewarmScopeMergesBuiltInStylesThatShareFontData) {
  static const EpdGlyph glyphs[] = {{4, 1, 16, 0, 1, 1, 0}, {4, 1, 16, 0, 1, 1, 1}, {4, 1, 16, 0, 1, 1, 2}};
  static const EpdUnicodeInterval interval{65, 67, 0};
  static const uint8_t bitmap[] = {1, 3, 0, 252, 255, 0, 64, 128};
  static const EpdFontGroup group{0, sizeof(bitmap), 3, 3, 0};
  EpdFontData regularData{};
  regularData.bitmap = bitmap;
  regularData.glyph = glyphs;
  regularData.intervals = &interval;
  regularData.intervalCount = 1;
  regularData.groups = &group;
  regularData.groupCount = 1;
  regularData.is2Bit = true;
  EpdFontData boldData = regularData;
  const EpdFontFamily family(&regularData, &boldData, &regularData, nullptr);
  FontRegistry builtinFonts{{5, family}};
  const std::map<int, SdCardFont*> noSdFonts;
  FontDecompressor decompressor;
  builtinFonts.attach(noSdFonts);
  FontCacheManager manager(builtinFonts, noSdFonts, kNoTtfFonts);
  manager.setFontDecompressor(&decompressor);

  auto scope = manager.createPrewarmScope();
  manager.recordText("A", 5, EpdFontFamily::REGULAR);
  manager.recordText("B", 5, EpdFontFamily::BOLD);
  manager.recordText("C", 5, EpdFontFamily::ITALIC);
  scope.endScanAndPrewarm();

  ASSERT_EQ(2U, decompressor.pageSlotViewCount());
  const auto* slots = static_cast<const XpFontCacheSlot*>(decompressor.pageSlotView());
  const auto& bold = slots[0];
  const auto& regular = slots[1];
  ASSERT_EQ(&boldData, bold.fontData);
  ASSERT_EQ(1, bold.glyphCount);
  EXPECT_EQ(1U, bold.glyphs[0].glyphIndex);
  ASSERT_EQ(&regularData, regular.fontData);
  ASSERT_EQ(2, regular.glyphCount);
  EXPECT_EQ(0U, regular.glyphs[0].glyphIndex);
  EXPECT_EQ(2U, regular.glyphs[1].glyphIndex);
}

TEST(FontCacheManagerTest, PrewarmScopePreservesUniqueMultibyteCodepoints) {
  SdCardFont font;
  FontRegistry noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{7, &font}};
  noBuiltinFonts.attach(sdFonts);
  FontCacheManager manager(noBuiltinFonts, sdFonts, kNoTtfFonts);

  auto scope = manager.createPrewarmScope();
  manager.recordText("\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80\xC3\xA9", 7, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();

  ASSERT_EQ(1, font.prewarmCallCount);
  EXPECT_STREQ("\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80", font.prewarmCalls[0].text);
}

TEST(FontCacheManagerTest, PrewarmScanDoesNotAllocateHeapMemory) {
  SdCardFont font;
  FontRegistry noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{7, &font}};
  noBuiltinFonts.attach(sdFonts);
  FontCacheManager manager(noBuiltinFonts, sdFonts, kNoTtfFonts);

  heapAllocationCount = 0;
  countHeapAllocations = true;
  auto scope = manager.createPrewarmScope();
  manager.recordText("Repeated text: \xC3\xA9 \xE4\xB8\xAD \xF0\x9F\x98\x80", 7, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();
  countHeapAllocations = false;

  EXPECT_EQ(0U, heapAllocationCount);
}

TEST(FontCacheManagerTest, IncrementalPrewarmRequestsAccumulation) {
  SdCardFont font;
  FontRegistry noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{7, &font}};
  noBuiltinFonts.attach(sdFonts);
  FontCacheManager manager(noBuiltinFonts, sdFonts, kNoTtfFonts);
  manager.prewarmCache(7, "title", 1);
  ASSERT_EQ(1, font.prewarmCallCount);
  EXPECT_TRUE(font.prewarmCalls[0].accumulate);
}

extern "C" uint8_t xp_native_font_compat_resolve(void* context, int32_t font, uint8_t style) {
  return static_cast<const std::map<int, SdCardFont*>*>(context)->at(font)->resolveStyle(style);
}
extern "C" int32_t xp_native_font_compat_prewarm(void* context, int32_t font, uint8_t backend, const char* text,
                                                 uint8_t styles, uint8_t metadata, uint8_t kernLig,
                                                 uint8_t accumulate) {
  EXPECT_EQ(0, backend);
  return static_cast<const std::map<int, SdCardFont*>*>(context)->at(font)->prewarm(text, styles, metadata != 0,
                                                                                    kernLig != 0, accumulate != 0);
}
extern "C" void xp_native_font_compat_clear(void* context, int32_t font, uint8_t backend, uint8_t resident) {
  EXPECT_EQ(0, backend);
  auto* cache = static_cast<const std::map<int, SdCardFont*>*>(context)->at(font);
  if (resident)
    cache->releaseResidentCaches();
  else
    cache->clearCache();
}
extern "C" void xp_native_font_compat_reset(void* context, int32_t font) {
  static_cast<const std::map<int, SdCardFont*>*>(context)->at(font)->resetStats();
}
extern "C" void xp_native_font_cache_diagnostic(const void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
