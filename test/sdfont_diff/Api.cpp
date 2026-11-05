#include <EpdFontData.h>
#include <xp_ffi.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

static_assert(sizeof(XpSdfontHeader) == 24);
static_assert(offsetof(XpSdfontHeader, ascender) == 10);
static_assert(offsetof(XpSdfontHeader, kernLeftEntryCount) == 16);
static_assert(sizeof(XpSdfontDirectory) == 8);
static_assert(sizeof(XpSdfontStyle) == 56);
static_assert(offsetof(XpSdfontStyle, offsets) == 24);
static_assert(offsetof(XpSdfontStyle, id) == 52);
static_assert(sizeof(XpSdfontIntervals) == 12);

int main() {
  for (unsigned id = 0; id < 256; ++id) {
    for (unsigned count = 0; count < 256; ++count) {
      for (uint16_t codepoint : {uint16_t(0), uint16_t(65535)}) {
        EpdKernClassEntry entry{codepoint, static_cast<uint8_t>(id)};
        uint32_t decoded = 0;
        const auto actual =
            xp_sdfont_kern_class(reinterpret_cast<const uint8_t*>(&entry), sizeof(entry), count, &decoded);
        assert(actual == (entry.classId <= count ? entry.classId : 0) && decoded == entry.codepoint);
      }
    }
  }
  for (unsigned pos = 0; pos < 8; ++pos) {
    for (unsigned byte = 0; byte < 256; ++byte) {
      uint8_t raw[8]{};
      raw[pos] = byte;
      EpdLigaturePair pair{};
      std::memcpy(&pair, raw, sizeof(pair));
      uint32_t left = 0, right = 0, out = 0;
      assert(xp_sdfont_ligature(raw, sizeof(raw), &left, &right, &out));
      assert(left == (pair.pair >> 16) && right == (pair.pair & 0xffff) && out == pair.ligatureCp);
    }
  }
  std::array<uint8_t, 32> bytes{};
  std::memcpy(bytes.data(), "CPFONT\0\0", 8);
  bytes[8] = 4;
  bytes[12] = 1;
  XpSdfontDirectory directory{};
  directory.hash = 42;
  for (size_t n = 0; n < 32; ++n) {
    assert(!xp_sdfont_header(bytes.data(), n, &directory));
    assert(directory.hash == 42);
  }
  assert(!xp_sdfont_header(nullptr, 32, &directory));
  assert(!xp_sdfont_header(bytes.data(), std::numeric_limits<size_t>::max(), &directory));
  assert(!xp_sdfont_header(bytes.data(), bytes.size(), nullptr));
  assert(xp_sdfont_header(bytes.data(), bytes.size(), &directory));
  XpSdfontStyle style{};
  bytes.fill(0);
  assert(xp_sdfont_style(bytes.data(), bytes.size(), &directory, &style));
  const auto hash = directory.hash;
  style.id = 77;
  assert(!xp_sdfont_style(bytes.data(), bytes.size(), &directory, &style));
  assert(directory.hash == hash && style.id == 77);
  XpSdfontIntervals intervals{0, 0, true};
  uint32_t iv[3] = {65, 66, 0};
  assert(xp_sdfont_interval(reinterpret_cast<uint8_t*>(iv), sizeof(iv), 2, 0, &intervals, iv));
  assert(iv[0] == 65 && iv[1] == 66 && iv[2] == 0 && intervals.expectedOffset == 2);
  assert(!xp_sdfont_interval(reinterpret_cast<uint8_t*>(iv), sizeof(iv), 2, 1, &intervals, iv));
  assert(intervals.expectedOffset == 2 && iv[0] == 65);
  uint8_t size = 77;
  assert(!xp_sdfont_filename(nullptr, 0, &size));
  assert(size == 77);
  uint32_t cp = 77, left = 88, right = 99;
  for (size_t n = 0; n < 3; ++n) {
    assert(xp_sdfont_kern_class(bytes.data(), n, 4, &cp) == 0 && cp == 77);
  }
  for (size_t n = 0; n < 8; ++n) {
    assert(!xp_sdfont_ligature(bytes.data(), n, &left, &right, &cp));
    assert(left == 88 && right == 99 && cp == 77);
  }
  for (size_t n = 0; n < 16; ++n) assert(!xp_sdfont_glyph_valid(bytes.data(), n, false, 0, 0));
}
