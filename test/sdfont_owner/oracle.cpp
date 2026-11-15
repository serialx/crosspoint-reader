#include <HalStorage.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#define private public
#include <SdCardFont.h>
#undef private

static void value(std::vector<uint8_t>& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(value & 255);
    value >>= 8;
  }
}
static std::vector<uint8_t> record() {
  SdCardFont font;
  std::vector<uint8_t> out;
  const bool ok = font.load("fixture.cpfont");
  value(out, ok);
  if (!ok) return out;
  value(out, font.contentHash());
  value(out, font.styleCount());
  for (uint8_t id = 0; id < 4; ++id) {
    value(out, font.hasStyle(id));
    if (!font.hasStyle(id)) continue;
    const auto& s = font.styleData(id);
    const auto& h = s.header;
    for (uint32_t v : {h.intervalCount, h.glyphCount, uint32_t(h.advanceY), uint32_t(h.ascender), uint32_t(h.descender),
                       uint32_t(h.is2Bit), uint32_t(h.kernLeftEntryCount), uint32_t(h.kernRightEntryCount),
                       uint32_t(h.kernLeftClassCount), uint32_t(h.kernRightClassCount), uint32_t(h.ligaturePairCount),
                       s.intervalsFileOffset, s.glyphsFileOffset, s.kernLeftFileOffset, s.kernRightFileOffset,
                       s.kernMatrixFileOffset, s.ligatureFileOffset, s.bitmapFileOffset, uint32_t(s.intervalsAreBmp16)})
      value(out, v);
    for (uint32_t j = 0; j < h.intervalCount; ++j) {
      if (s.intervalsAreBmp16) {
        value(out, s.bmpIntervals[j].first);
        value(out, s.bmpIntervals[j].last);
        value(out, s.bmpIntervals[j].offset);
      } else {
        value(out, s.fullIntervals[j].first);
        value(out, s.fullIntervals[j].last);
        value(out, s.fullIntervals[j].offset);
      }
    }
  }
  return out;
}
int main() {
  uint8_t length[4];
  while (std::fread(length, 1, 4, stdin) == 4) {
    uint32_t n = 0;
    for (int i = 3; i >= 0; --i) n = (n << 8) | length[i];
    sdFontTestFile.resize(n);
    if (std::fread(sdFontTestFile.data(), 1, n, stdin) != n) return 2;
    const auto out = record();
    for (int i = 0; i < 4; ++i) length[i] = out.size() >> (8 * i);
    if (std::fwrite(length, 1, 4, stdout) != 4 || std::fwrite(out.data(), 1, out.size(), stdout) != out.size())
      return 3;
  }
  return std::ferror(stdin) ? 4 : 0;
}
