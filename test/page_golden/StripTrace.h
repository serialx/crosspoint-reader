#pragma once
#include <xp_page_paint.h>

#include <vector>
using StripBounds = XpPageRowBounds;
struct StripStats {
  uint64_t records, drawn, glyphs;
};
#ifdef XP_STRIP_SPIKE
extern std::vector<StripBounds> stripBounds;
extern StripStats stripStats;
extern int stripPhase;
#endif
uint32_t pageGoldenPaint(const uint8_t*, size_t, const XpPageTarget*, const XpPagePaint*);
extern "C" void xp_strip_packed_row(const XpImageTarget*, int32_t, int32_t, int32_t, const uint8_t*, size_t);
