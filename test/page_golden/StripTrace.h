#pragma once
#include <xp_ffi.h>

#include <vector>
struct StripBounds {
  int32_t start, end;
};
struct StripStats {
  uint64_t records, drawn, glyphs;
};
extern std::vector<StripBounds> stripBounds;
extern StripStats stripStats;
extern int stripPhase;
extern "C" uint32_t xp_page_strip_render(const uint8_t*, size_t, const XpNativeProvider*, const XpPageRenderServices*,
                                         XpGlyphPaint*, int32_t, int32_t, int32_t, uint8_t, StripBounds*, size_t,
                                         StripStats*, uint8_t);
extern "C" void xp_strip_packed_row(const XpImageTarget*, int32_t, int32_t, int32_t, const uint8_t*, size_t);

#ifdef XP_STRIP_PLAN
uint32_t pageGoldenPlan(const uint8_t*, size_t, const XpNativeProvider*, const XpPageRenderServices*, XpGlyphPaint*,
                        int32_t, int32_t, int32_t, uint8_t);
#endif
