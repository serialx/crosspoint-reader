#pragma once
#include <HalStorage.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <vector>

#include "builtinFonts/notosans_12_bold.h"
#include "builtinFonts/notosans_12_bolditalic.h"
#include "builtinFonts/notosans_12_italic.h"
#include "builtinFonts/notosans_12_regular.h"
#include "builtinFonts/ubuntu_12_regular.h"

inline void require(bool ok, const char* message) {
  if (!ok) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}
inline std::vector<std::string> corpus() {
  std::vector<std::string> result = {"",
                                     "A",
                                     "AVATAR",
                                     "To WA.",
                                     "office ffi ffl fi fl",
                                     "a\xcc\x81V",
                                     "\u0301A",
                                     "École déjà vu",
                                     "한국어 데미안",
                                     "漢字 日本語",
                                     "العَرَبِيَّة لا لإ لآ",
                                     "שָׁלוֹם יְהוָה",
                                     "█■",
                                     "\u02bb",
                                     "\ufffd\U0001f600",
                                     "a b\u00a0c\u3000d",
                                     "\u0301\u0327",
                                     "a\u05b0V",
                                     "\ufedf\ufe8e",
                                     "fj Áj",
                                     "\u03c2\u0430\u0131\u00df"};
  constexpr uint32_t pool[] = {'A',   'V',    'T',    'o',    'f',    'i',   'l',    'a',     'j',    ' ',
                               0xa0,  0x3000, 0x301,  0x327,  0x591,  0x5bc, 0x5c1,  0x64b,   0x628,  0x644,
                               0x627, 0xac00, 0x4e00, 0x2588, 0x25a0, 0x2bb, 0xfffd, 0x1f600, 0xfb00, 0xfe8e};
  uint32_t seed = 0x5212026;
  for (int i = 0; i < 150; ++i) {
    std::string text;
    for (int j = 0; j < i % 17 + 1; ++j) {
      seed = seed * 1664525 + 1013904223;
      utf8AppendCodepoint(pool[seed % std::size(pool)], text);
    }
    result.push_back(text);
  }
  return result;
}
inline void put16(size_t at, uint16_t v) {
  sdFontTestFile[at] = v;
  sdFontTestFile[at + 1] = v >> 8;
}
inline void put32(size_t at, uint32_t v) {
  put16(at, v);
  put16(at + 2, v >> 16);
}
// Public, generated v4 SD font: fractional advances, zero-width marks and missing solids.
inline void makeSdFont() {
  std::set<uint32_t> cps;
  for (uint32_t cp = 32; cp < 127; ++cp) cps.insert(cp);
  for (auto cp :
       {0xa0, 0x301, 0x327, 0x591, 0x5bc, 0x5c1, 0x64b, 0x628, 0x644, 0x627, 0xac00, 0x4e00, 0xfffd, 0xfb00, 0xfe8e})
    cps.insert(cp);
  size_t count = cps.size(), offset = 64, glyphOffset = offset + count * 12,
         bitmapOffset = glyphOffset + count * sizeof(EpdGlyph);
  sdFontTestFile.assign(bitmapOffset + count * 8, 0);
  std::memcpy(sdFontTestFile.data(), "CPFONT\0\0", 8);
  put16(8, CPFONT_VERSION);
  sdFontTestFile[12] = 1;
  put32(36, count);
  put32(40, count);
  sdFontTestFile[44] = 24;
  put16(45, 19);
  put16(47, -5);
  put32(56, offset);
  size_t i = 0;
  for (auto cp : cps) {
    put32(offset + i * 12, cp);
    put32(offset + i * 12 + 4, cp);
    put32(offset + i * 12 + 8, i);
    EpdGlyph glyph{8, 8, static_cast<uint16_t>(100 + cp % 31), -2, 11, 8, static_cast<uint32_t>(i * 8)};
    if (cp == 0x301) glyph.advanceX = 0;
    std::memcpy(sdFontTestFile.data() + glyphOffset + i * sizeof(glyph), &glyph, sizeof(glyph));
    ++i;
  }
}
