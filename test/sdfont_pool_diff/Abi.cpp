#include <atomic>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#define private public
#include <SdCardFont.h>
#undef private
int main() {
  using S = SdCardFont::PerStyle;
  using O = SdCardFont::OverflowEntry;
  constexpr size_t values[] = {sizeof(S),
                               offsetof(S, header),
                               offsetof(S, intervalsFileOffset),
                               offsetof(S, fullIntervals),
                               offsetof(S, bmpIntervals),
                               offsetof(S, intervalsAreBmp16),
                               offsetof(S, ligaturePairs),
                               offsetof(S, ligaturesLoaded),
                               offsetof(S, kernBlockIndex),
                               offsetof(S, kernBlockIndexReady),
                               offsetof(S, stubData),
                               offsetof(S, miniData),
                               offsetof(S, miniIntervals),
                               offsetof(S, miniGlyphs),
                               offsetof(S, miniBitmap),
                               offsetof(S, miniIntervalCount),
                               offsetof(S, miniGlyphCount),
                               offsetof(S, miniIntervalCapacity),
                               offsetof(S, miniGlyphCapacity),
                               offsetof(S, miniBitmapCapacity),
                               offsetof(S, miniBitmapUsed),
                               offsetof(S, measuredBytesPerGlyph),
                               offsetof(S, miniUnderuseRuns),
                               offsetof(S, miniMetadataOnly),
                               offsetof(S, miniHysteresisPending),
                               offsetof(S, miniKernLeftClasses),
                               offsetof(S, miniKernRightClasses),
                               offsetof(S, miniKernLeftEntryCount),
                               offsetof(S, miniKernRightEntryCount),
                               offsetof(S, miniKernLeftClassCount),
                               offsetof(S, miniKernRightClassCount),
                               offsetof(S, miniKernMatrix),
                               offsetof(S, miniKernLeftCapacity),
                               offsetof(S, miniKernRightCapacity),
                               offsetof(S, miniKernMatrixCapacity),
                               offsetof(S, miniKernBuilt),
                               offsetof(S, epdFont),
                               sizeof(O),
                               offsetof(O, bitmap),
                               offsetof(O, codepoint),
                               offsetof(O, styleIdx),
                               sizeof(SdCardFont::Stats),
                               sizeof(XpSdfontPool),
                               sizeof(XpSdfontServices),
                               sizeof(XpSdfontTexts)};

#if UINTPTR_MAX == UINT32_MAX
  static_assert(values[0] == 400);
  static_assert(values[1] == 0);
  static_assert(values[2] == 24);
  static_assert(values[3] == 52);
  static_assert(values[4] == 56);
  static_assert(values[5] == 60);
  static_assert(values[6] == 64);
  static_assert(values[7] == 68);
  static_assert(values[8] == 72);
  static_assert(values[9] == 76);
  static_assert(values[10] == 80);
  static_assert(values[11] == 200);
  static_assert(values[12] == 320);
  static_assert(values[13] == 324);
  static_assert(values[14] == 328);
  static_assert(values[15] == 332);
  static_assert(values[16] == 336);
  static_assert(values[17] == 340);
  static_assert(values[18] == 344);
  static_assert(values[19] == 348);
  static_assert(values[20] == 352);
  static_assert(values[21] == 356);
  static_assert(values[22] == 360);
  static_assert(values[23] == 361);
  static_assert(values[24] == 362);
  static_assert(values[25] == 364);
  static_assert(values[26] == 368);
  static_assert(values[27] == 372);
  static_assert(values[28] == 374);
  static_assert(values[29] == 376);
  static_assert(values[30] == 377);
  static_assert(values[31] == 380);
  static_assert(values[32] == 384);
  static_assert(values[33] == 386);
  static_assert(values[34] == 388);
  static_assert(values[35] == 392);
  static_assert(values[36] == 396);
  static_assert(values[37] == 28);
  static_assert(values[38] == 16);
  static_assert(values[39] == 20);
  static_assert(values[40] == 24);
  static_assert(values[41] == 20);
  static_assert(values[42] == 48);
  static_assert(values[43] == 36);
  static_assert(values[44] == 20);
#endif
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
    if (values[i] != xp_sdfont_pool_layout(i)) {
      std::fprintf(stderr, "ABI field %zu: C++=%zu Rust=%zu\n", i, values[i], xp_sdfont_pool_layout(i));
      return 1;
    }
  }
}
