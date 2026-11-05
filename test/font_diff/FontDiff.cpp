#include <EpdFont.h>
#include <HalStorage.h>
#include <Utf8.h>
#include <builtinFonts/all.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Layout.h"

// Test-only inspection; production cache ownership and visibility stay unchanged.
#define private public
#include <SdCardFont.h>
#undef private

#include "SdLayout.h"

extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_allocation_attempts();

namespace {
uint64_t hash = 14695981039346656037ull;
uint64_t glyphCases = 0, kernCases = 0, ligatureCases = 0;
void check(bool ok, int line) {
  if (!ok) {
    std::fprintf(stderr, "FontDiff failure at line %d\n", line);
    std::abort();
  }
}
#define require(ok) check((ok), __LINE__)
void record(uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    hash = (hash ^ (value & 255)) * 1099511628211ull;
    value >>= 8;
  }
}
void recordGlyph(const EpdGlyph* glyph) {
  record(glyph != nullptr);
  if (!glyph) return;
  record(glyph->width);
  record(glyph->height);
  record(glyph->advanceX);
  record(glyph->left);
  record(glyph->top);
  record(glyph->dataLength);
  record(glyph->dataOffset);
}
bool sameGlyph(const EpdGlyph& a, const XpFontGlyph& b) {
  return a.width == b.width && a.height == b.height && a.advanceX == b.advanceX && a.left == b.left && a.top == b.top &&
         a.dataLength == b.dataLength && a.dataOffset == b.dataOffset;
}
bool resident(const EpdFontData& d, uint32_t cp) {
  for (uint32_t i = 0; i < d.intervalCount; ++i) {
    if (cp >= d.intervals[i].first && cp <= d.intervals[i].last) return true;
  }
  return false;
}
void checkGlyph(const EpdFont& font, uint32_t cp, bool metrics) {
  XpFontGlyph actual{};
  const uint32_t status = xp_font_glyph(font.data, cp, metrics, &actual);
  if (font.data->glyphMissHandler && !resident(*font.data, cp)) {
    // Generic handler-backed views cannot answer coverage or load a glyph.
    require(status == XP_FONT_NOT_RESIDENT);
    return;
  }
  EpdGlyph fallback{};
  const auto* expected = metrics ? font.getGlyphMetrics(cp, fallback) : font.getGlyph(cp);
  require(status == (expected == &fallback ? XP_FONT_SYNTHETIC : expected ? XP_FONT_RESIDENT : XP_FONT_MISSING));
  if (expected) require(sameGlyph(*expected, actual));
}
XpFontSdView sdView(SdCardFont& font, uint8_t style) {
  const auto& s = font.styleData(style);
  return {font.getEpdFont(style)->data,
          font.advanceTable_[style],
          font.advanceTableSize_[style],
          s.fullIntervals,
          s.bmpIntervals,
          s.header.intervalCount};
}
void checkSd(SdCardFont& font, uint8_t style) {
  const auto view = sdView(font, style);
  const auto* epd = font.getEpdFont(style);
  const size_t reads = sdFontTestReads;
  for (uint32_t cp = 0; cp <= 0x10ffff; ++cp) {
    require(xp_font_sd_has_codepoint(&view, cp) == epd->hasCodepoint(cp));
    uint16_t advance = 999;
    const bool found = xp_font_sd_advance(&view, cp, &advance);
    uint16_t expected = 999;
    require(found == font.advanceTableLookup(style, cp, &expected));
    require(advance == expected);
    XpFontGlyph glyph{};
    const uint32_t status = xp_font_sd_glyph(&view, cp, false, &glyph);
    const bool inRam = resident(*epd->data, cp);
    require(status == (inRam ? XP_FONT_RESIDENT : XP_FONT_NOT_RESIDENT));
    if (inRam) require(sameGlyph(*epd->getGlyph(cp), glyph));
  }
  require(sdFontTestReads == reads);
  for (uint32_t cp : {0x2588u, 0x25a0u}) {
    XpFontGlyph glyph{};
    const uint32_t status = xp_font_sd_glyph(&view, cp, true, &glyph);
    if (status == XP_FONT_NOT_RESIDENT) continue;
    EpdGlyph fallback{};
    const auto* expected = epd->getGlyphMetrics(cp, fallback);
    require(expected && sameGlyph(*expected, glyph));
    require(status == (expected == &fallback ? XP_FONT_SYNTHETIC : XP_FONT_RESIDENT));
  }
}
std::string utf8(uint32_t cp) {
  char bytes[5] = {};
  utf8AppendCodepoint(bytes, cp);
  return bytes;
}
void glyphCase(const EpdFont& font, uint32_t cp) {
  checkGlyph(font, cp, false);
  checkGlyph(font, cp, true);
  const auto covered = xp_font_has_codepoint(font.data, cp);
  if (resident(*font.data, cp) || !font.data->coverageHandler)
    require(covered == font.hasCodepoint(cp));
  else
    require(covered == -1);
  ++glyphCases;
  record(cp);
  record(font.hasCodepoint(cp));
  recordGlyph(font.getGlyph(cp));
  EpdGlyph fallback{};
  recordGlyph(font.getGlyphMetrics(cp, fallback));
}
void kernCase(const EpdFont& font, uint32_t left, uint32_t right) {
  int8_t kern = 0;
  require(xp_font_kerning(font.data, left, right, &kern));
  require(kern == font.getKerning(left, right));
  ++kernCases;
  record(left);
  record(right);
  record(static_cast<uint8_t>(font.getKerning(left, right)));
}
void ligatureCase(const EpdFont& font, uint32_t cp, const std::string& text) {
  ++ligatureCases;
  const char* tail = text.c_str();
  record(cp);
  const uint32_t expected = font.applyLigatures(cp, tail);
  size_t consumed = 0;
  require(xp_font_apply_ligatures(font.data, cp, reinterpret_cast<const uint8_t*>(text.data()), text.size(),
                                  &consumed) == expected);
  require(consumed == static_cast<size_t>(tail - text.c_str()));
  record(expected);
  record(static_cast<uint32_t>(tail - text.c_str()));
}
void tables(const EpdFont& font) {
  const auto& d = *font.data;
  for (uint32_t i = 0; i < d.intervalCount; ++i) {
    const auto& iv = d.intervals[i];
    for (uint32_t cp = iv.first; cp <= iv.last; ++cp) glyphCase(font, cp);
    if (iv.first) glyphCase(font, iv.first - 1);
    if (iv.last < UINT32_MAX) glyphCase(font, iv.last + 1);
  }
  for (uint32_t cp : {0u, 0x2bbu, 0x2018u, 0x2588u, 0x25a0u, 0xfffdu, 0xffffu, 0x10000u, 0x10ffffu, UINT32_MAX}) {
    glyphCase(font, cp);
  }
  for (uint16_t i = 0; i < d.kernLeftEntryCount; ++i) {
    const uint32_t left = d.kernLeftCodepoints ? d.kernLeftCodepoints[i] : d.kernLeftClasses[i].codepoint;
    for (uint16_t j = 0; j < d.kernRightEntryCount; ++j) {
      const uint32_t right = d.kernRightCodepoints ? d.kernRightCodepoints[j] : d.kernRightClasses[j].codepoint;
      kernCase(font, left, right);
    }
    for (uint32_t right : {0u, 0x4e00u, 0x10000u, UINT32_MAX}) kernCase(font, left, right);
  }
  for (uint32_t left : {0u, 0x4e00u, 0x10000u, UINT32_MAX}) kernCase(font, left, 'A');
  for (uint32_t i = 0; i < d.ligaturePairCount && d.ligaturePairs; ++i) {
    const auto pair = d.ligaturePairs[i];
    const uint32_t left = pair.pair >> 16, right = pair.pair & 65535;
    require(xp_font_ligature(font.data, left, right) == font.getLigature(left, right));
    record(font.getLigature(left, right));
    ligatureCase(font, left, utf8(right));
    ligatureCase(font, left, utf8(right) + "!");
    for (uint32_t j = 0; j < d.ligaturePairCount; ++j) {
      const auto next = d.ligaturePairs[j];
      if ((next.pair >> 16) == pair.ligatureCp) ligatureCase(font, left, utf8(right) + utf8(next.pair & 65535));
    }
  }
  for (uint32_t cp : {0x66u, 0xfeedu, 0xfb50u, 0x10000u}) {
    for (const auto* tail : {"", "ffi!", "\xef\xbb\x8e", "\xc0\xaf", "\xf0\x9f", "\xff!"}) ligatureCase(font, cp, tail);
  }
}
const EpdGlyph* forbiddenMiss(void*, uint32_t) { std::abort(); }
bool forbiddenCoverage(void*, uint32_t) { std::abort(); }
int8_t forbiddenKern(void*, uint32_t, uint32_t) { std::abort(); }
void edgeCases() {
  EpdFontData data{};
  XpFontGlyph glyph{};
  require(xp_font_glyph(&data, 'A', false, &glyph) == XP_FONT_MISSING);
  for (int ascender : {-4, 0, 1, 8, 300}) {
    data.ascender = ascender;
    checkGlyph(EpdFont(&data), 0x2588, true);
    checkGlyph(EpdFont(&data), 0x25a0, true);
  }
  const EpdGlyph aliases[] = {{3, 4, 100, 0, 4, 0, 0}, {5, 6, 120, 1, 6, 0, 0}};
  const EpdUnicodeInterval aliasIntervals[] = {{0x2018, 0x2018, 0}, {0xfffd, 0xfffd, 1}};
  data.glyph = aliases;
  data.intervals = aliasIntervals;
  data.intervalCount = 2;
  checkGlyph(EpdFont(&data), 0x02bb, false);
  checkGlyph(EpdFont(&data), 0x10000, false);
  data.intervalCount = 0;
  data.glyphMissHandler = forbiddenMiss;
  data.coverageHandler = forbiddenCoverage;
  data.kernHandler = forbiddenKern;
  require(xp_font_glyph(&data, 'A', false, &glyph) == XP_FONT_NOT_RESIDENT);
  require(xp_font_glyph(&data, 0x25a0, true, &glyph) == XP_FONT_NOT_RESIDENT);
  require(xp_font_has_codepoint(&data, 'A') == -1);
  int8_t kern = 123;
  require(!xp_font_kerning(&data, 'A', 'V', &kern));
  require(kern == 123);
  require(xp_font_kerning(&data, 0x4e00, 'V', &kern) && kern == 0);
  // An absent optional table can retain its count after a failed lazy load.
  data.ligaturePairCount = 10;
  require(xp_font_ligature(&data, 'f', 'i') == 0);
  size_t consumed = 999;
  require(xp_font_apply_ligatures(&data, 'f', nullptr, 0, &consumed) == 'f' && consumed == 0);
  const EpdGlyph em = {4, 4, 128, 0, 4, 0, 0};
  const EpdUnicodeInterval interval = {'M', 'M', 0};
  data.glyph = &em;
  data.intervals = &interval;
  data.intervalCount = 1;
  const EpdUnicodeInterval full[] = {{'M', 'M', 0}, {0x25a0, 0x25a0, 1}};
  const XpFontSdView view = {&data, nullptr, 0, full, nullptr, 2};
  require(xp_font_sd_glyph(&view, 0x2588, true, &glyph) == XP_FONT_SYNTHETIC);
  require(xp_font_sd_glyph(&view, 0x25a0, true, &glyph) == XP_FONT_NOT_RESIDENT);
  data.intervalCount = 0;
  require(xp_font_sd_glyph(&view, 0x2588, true, &glyph) == XP_FONT_NOT_RESIDENT);
}
void summary(const char* name) {
  std::printf("%s glyph=%llu kern=%llu lig=%llu hash=%016llx\n", name, static_cast<unsigned long long>(glyphCases),
              static_cast<unsigned long long>(kernCases), static_cast<unsigned long long>(ligatureCases),
              static_cast<unsigned long long>(hash));
}
void sdFont(const char* path) {
  std::ifstream file(path, std::ios::binary);
  require(file.good());
  sdFontTestFile.assign(std::istreambuf_iterator<char>(file), {});
  SdCardFont font;
  require(font.load("fixture.cpfont"));
  const char* text = "AVATAR office ffi fl M solid ▉ █ ■ 한글 데미안 中文 日本語 العربية العَرَبِيَّة עברית \u02bb";
  require(font.buildAdvanceTable(text) >= 0);
  for (uint8_t style = 0; style < 4; ++style) {
    if (!font.hasStyle(style)) continue;
    checkSd(font, style);
    record(style);
    for (uint32_t i = 0; i < font.advanceTableSize_[style]; ++i) {
      const auto& entry = font.advanceTable_[style][i];
      require(font.getAdvance(entry.codepoint, style) == entry.advanceX);
      record(entry.codepoint);
      record(entry.advanceX);
    }
  }
  require(font.prewarm(text) >= 0);
  for (uint8_t style = 0; style < 4; ++style) {
    if (!font.hasStyle(style)) continue;
    checkSd(font, style);
    tables(*font.getEpdFont(style));
  }
  font.clearCache();
  font.releaseResidentCaches();
  for (uint8_t style = 0; style < 4; ++style) {
    if (font.hasStyle(style)) {
      checkSd(font, style);
      tables(*font.getEpdFont(style));
    }
  }
  summary(path);
}
}  // namespace

int main(int argc, char** argv) {
  xp_test_fail_allocation_after(0);
  edgeCases();
  tables(EpdFont(&notosans_12_bold));
  summary("notosans_12_bold");
  tables(EpdFont(&notosans_12_bolditalic));
  summary("notosans_12_bolditalic");
  tables(EpdFont(&notosans_12_italic));
  summary("notosans_12_italic");
  tables(EpdFont(&notosans_12_regular));
  summary("notosans_12_regular");
  tables(EpdFont(&notosans_14_bold));
  summary("notosans_14_bold");
  tables(EpdFont(&notosans_14_bolditalic));
  summary("notosans_14_bolditalic");
  tables(EpdFont(&notosans_14_italic));
  summary("notosans_14_italic");
  tables(EpdFont(&notosans_14_regular));
  summary("notosans_14_regular");
  tables(EpdFont(&notosans_16_bold));
  summary("notosans_16_bold");
  tables(EpdFont(&notosans_16_bolditalic));
  summary("notosans_16_bolditalic");
  tables(EpdFont(&notosans_16_italic));
  summary("notosans_16_italic");
  tables(EpdFont(&notosans_16_regular));
  summary("notosans_16_regular");
  tables(EpdFont(&notosans_18_bold));
  summary("notosans_18_bold");
  tables(EpdFont(&notosans_18_bolditalic));
  summary("notosans_18_bolditalic");
  tables(EpdFont(&notosans_18_italic));
  summary("notosans_18_italic");
  tables(EpdFont(&notosans_18_regular));
  summary("notosans_18_regular");
  tables(EpdFont(&ubuntu_10_bold));
  summary("ubuntu_10_bold");
  tables(EpdFont(&ubuntu_10_regular));
  summary("ubuntu_10_regular");
  tables(EpdFont(&ubuntu_12_bold));
  summary("ubuntu_12_bold");
  tables(EpdFont(&ubuntu_12_regular));
  summary("ubuntu_12_regular");
  tables(EpdFont(&ubuntu_8_regular));
  summary("ubuntu_8_regular");
  for (int i = 1; i < argc; ++i) sdFont(argv[i]);
  require(xp_test_allocation_attempts() == 0);
}
