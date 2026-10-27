#include <HalStorage.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#define private public
#include <SdCardFont.h>
#undef private

namespace {
uint64_t hash;
void value(uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    hash = (hash ^ (v & 255)) * 1099511628211ULL;
    v >>= 8;
  }
}
void record() {
  SdCardFont font;
  hash = 14695981039346656037ULL;
  const bool ok = font.load("font.cpfont");
  value(ok);
  if (ok) {
    value(font.contentHash());
    value(font.styleCount());
    for (uint8_t i = 0; i < 4; ++i) {
      value(font.hasStyle(i));
      if (!font.hasStyle(i)) continue;
      const auto& s = font.styleData(i);
      const auto& h = s.header;
      for (uint32_t v :
           {h.intervalCount, h.glyphCount, uint32_t(h.advanceY), uint32_t(h.ascender), uint32_t(h.descender),
            uint32_t(h.is2Bit), uint32_t(h.kernLeftEntryCount), uint32_t(h.kernRightEntryCount),
            uint32_t(h.kernLeftClassCount), uint32_t(h.kernRightClassCount), uint32_t(h.ligaturePairCount),
            s.intervalsFileOffset, s.glyphsFileOffset, s.kernLeftFileOffset, s.kernRightFileOffset,
            s.kernMatrixFileOffset, s.ligatureFileOffset, s.bitmapFileOffset, uint32_t(s.intervalsAreBmp16)})
        value(v);
      for (uint32_t j = 0; j < h.intervalCount; ++j) {
        if (s.intervalsAreBmp16) {
          value(s.bmpIntervals[j].first);
          value(s.bmpIntervals[j].last);
          value(s.bmpIntervals[j].offset);
        } else {
          value(s.fullIntervals[j].first);
          value(s.fullIntervals[j].last);
          value(s.fullIntervals[j].offset);
        }
      }
    }
  }
  std::printf("%d %016llx\n", ok, static_cast<unsigned long long>(hash));
}
void corpus(const std::vector<uint8_t>& original) {
  sdFontTestFile = original;
  record();
  const size_t region = std::min<size_t>(160, original.size());
  for (size_t n = 0; n <= region; ++n) {
    sdFontTestFile.assign(original.begin(), original.begin() + n);
    record();
  }
  sdFontTestFile = original;
  for (size_t n = 0; n < region; ++n) {
    for (unsigned bit = 0; bit < 8; ++bit) {
      sdFontTestFile[n] ^= 1u << bit;
      record();
      sdFontTestFile[n] ^= 1u << bit;
    }
  }
  const auto read32 = [&](size_t p) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= uint32_t(original[p + i]) << (i * 8);
    return value;
  };
  for (size_t toc = 32; toc + 32 <= region; toc += 32) {
    const uint32_t count = read32(toc + 4), offset = read32(toc + 24);
    if (!count || count > 4096 || uint64_t(offset) + uint64_t(count) * 12 > original.size()) continue;
    for (size_t recordOffset : {size_t(offset), size_t(offset) + (count - 1) * 12}) {
      for (size_t p = recordOffset; p < recordOffset + 12; ++p) {
        for (unsigned bit = 0; bit < 8; ++bit) {
          sdFontTestFile[p] ^= 1u << bit;
          record();
          sdFontTestFile[p] ^= 1u << bit;
        }
      }
    }
  }
  for (size_t n = 32; n + 32 <= region; n += 32) {
    for (size_t offset : {size_t(4), size_t(8), size_t(24)}) {
      for (uint32_t v : {0u, 1u, 31u, 32u, 63u, 64u, 4096u, 4097u, 65535u, 65536u, 65537u, uint32_t(original.size()),
                         uint32_t(original.size() + 1), 0xfffffff0u, 0xffffffffu}) {
        for (unsigned j = 0; j < 4; ++j) sdFontTestFile[n + offset + j] = v >> (j * 8);
        record();
      }
      std::copy_n(original.begin() + n + offset, 4, sdFontTestFile.begin() + n + offset);
    }
  }
}
}  // namespace
void glyphs() {
  SdCardFont font;
  SdCardFont::PerStyle style;
  EpdGlyph glyph{};
  hash = 14695981039346656037ULL;
  for (bool twoBit : {false, true}) {
    style.header.is2Bit = twoBit;
    for (uint32_t width = 0; width < 256; ++width) {
      for (uint32_t height : {0u, 1u, 7u, 8u, 31u, 255u}) {
        glyph.width = width;
        glyph.height = height;
        const uint32_t packed = twoBit ? (width * height + 3) / 4 : (width * height + 7) / 8;
        for (uint32_t length : {0u, packed, packed + 1, 65535u}) {
          glyph.dataLength = length;
          for (uint32_t offset : {0u, 1u, 0xfffffff0u, 0xffffffffu}) {
            glyph.dataOffset = offset;
            for (uint32_t base : {0u, 64u, 0xffffffffu}) {
              style.bitmapFileOffset = base;
              for (uint32_t fileSize : {0u, 64u, 65536u, 0xffffffffu}) {
                font.fileSize_ = fileSize;
                value(font.glyphEntryValid(style, glyph, 65));
              }
            }
          }
        }
      }
    }
  }
  std::printf("glyphs %016llx\n", static_cast<unsigned long long>(hash));
}
int main(int argc, char** argv) {
  glyphs();
  for (int i = 1; i < argc; ++i) {
    std::ifstream file(argv[i], std::ios::binary);
    if (!file) return 2;
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    corpus(bytes);
  }
}
