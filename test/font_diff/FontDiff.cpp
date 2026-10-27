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

// Test-only inspection; production cache ownership and visibility stay unchanged.
#define private public
#include <SdCardFont.h>
#undef private

namespace {
uint64_t hash = 14695981039346656037ull;
uint64_t glyphCases = 0, kernCases = 0, ligatureCases = 0;
void require(bool ok) {
  if (!ok) std::abort();
}
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
std::string utf8(uint32_t cp) {
  char bytes[5] = {};
  utf8AppendCodepoint(bytes, cp);
  return bytes;
}
void glyphCase(const EpdFont& font, uint32_t cp) {
  ++glyphCases;
  record(cp);
  record(font.hasCodepoint(cp));
  recordGlyph(font.getGlyph(cp));
  EpdGlyph fallback{};
  recordGlyph(font.getGlyphMetrics(cp, fallback));
}
void kernCase(const EpdFont& font, uint32_t left, uint32_t right) {
  ++kernCases;
  record(left);
  record(right);
  record(static_cast<uint8_t>(font.getKerning(left, right)));
}
void ligatureCase(const EpdFont& font, uint32_t cp, const std::string& text) {
  ++ligatureCases;
  const char* tail = text.c_str();
  record(cp);
  record(font.applyLigatures(cp, tail));
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
    tables(*font.getEpdFont(style));
  }
  font.clearCache();
  font.releaseResidentCaches();
  for (uint8_t style = 0; style < 4; ++style) {
    if (font.hasStyle(style)) tables(*font.getEpdFont(style));
  }
  summary(path);
}
}  // namespace

int main(int argc, char** argv) {
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
}
