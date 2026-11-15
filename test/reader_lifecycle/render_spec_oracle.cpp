#include <Epub/ReaderRenderSpec.h>
#include <xp_epub_session.h>
#include <xp_ffi.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#include "../section_io/SectionTestCodec.h"
#include "render_spec_parent.inc"

extern "C" void xp_epub_test_section_spec(const XpEpubRenderSpec*, uint32_t (*)[20], uint8_t (*)[6]);
static void check(const XpEpubRenderSpec& raw, unsigned field, unsigned value) {
  const auto native = renderSpec(raw);
  const auto expected = headerFields(native, 3);
  uint32_t fields[20]{};
  uint8_t flags[6]{};
  xp_epub_test_section_spec(&raw, &fields, &flags);
  const std::array<uint8_t, 6> expectedFlags{native.extraParagraphSpacing, native.hyphenationEnabled,
                                             native.focusReadingEnabled,   native.embeddedStyle,
                                             native.embeddedStyle,         native.imageRendering};
  if (!std::equal(expected.begin(), expected.end(), fields) ||
      !std::equal(expectedFlags.begin(), expectedFlags.end(), flags)) {
    std::fprintf(stderr, "render flag mismatch field=%u value=%u images=%u\n", field, value, raw.images);
    std::abort();
  }
  XpSecRecord parent{}, candidate{};
  parent.kind = candidate.kind = XP_SEC_HEADER;
  std::copy(expected.begin(), expected.end(), parent.values);
  std::copy(std::begin(fields), std::end(fields), candidate.values);
  uint8_t parentBytes[44], candidateBytes[44];
  size_t parentSize = 0, candidateSize = 0;
  assert(xp_sec_encode_record(&parent, parentBytes, sizeof(parentBytes), &parentSize) == XP_SEC_OK);
  assert(xp_sec_encode_record(&candidate, candidateBytes, sizeof(candidateBytes), &candidateSize) == XP_SEC_OK);
  assert(parentSize == 44 && candidateSize == 44 && std::memcmp(parentBytes, candidateBytes, 44) == 0);
}
int main() {
  unsigned cases = 0;
  for (unsigned field = 0; field < 4; ++field) {
    for (unsigned value = 0; value <= 255; ++value) {
      for (uint8_t image : {0, 1, 2}) {
        XpEpubRenderSpec raw{16, 1.0f, 240, 320, 1, 2, -3, 110, 2, 0, 1, image, 1};
        uint8_t* flags[]{&raw.paragraph_spacing, &raw.hyphenation, &raw.embedded_style, &raw.focus_reading};
        *flags[field] = value;
        check(raw, field, value);
        ++cases;
      }
    }
  }
  for (unsigned value = 0; value <= 255; ++value) {
    XpEpubRenderSpec raw{16, 1.0f, 240, 320, 255, 2, -3, 110, 2, 255, 255, static_cast<uint8_t>(value), 255};
    check(raw, 4, value);
    ++cases;
  }
  printf("PASS %u parent-matched header bytes and Chapter options; every boolean byte and unchanged image mode\n",
         cases);
}
