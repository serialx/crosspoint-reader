#include <EpdFontData.h>
#include <xp_ffi.h>
#include <xp_page_paint.h>

#include <cstddef>

static_assert(sizeof(XpRasterGlyph) == sizeof(EpdGlyph));
static_assert(offsetof(XpRasterGlyph, dataOffset) == offsetof(EpdGlyph, dataOffset));
static_assert(sizeof(XpGlyphDraw) == 44);
static_assert(offsetof(XpGlyphDraw, x) == 16);
static_assert(offsetof(XpGlyphDraw, kind) == 40);
static_assert(sizeof(XpGlyphClip) == 16);
static_assert(sizeof(XpGlyphPageEntry) == 12);
static_assert(sizeof(XpGlyphTarget) == (sizeof(void*) == 4 ? 52 : 56));
static_assert(offsetof(XpGlyphTarget, clip) == (sizeof(void*) == 4 ? 28 : 32));
static_assert(offsetof(XpGlyphTarget, orientation) == (sizeof(void*) == 4 ? 44 : 48));
static_assert(sizeof(XpGlyphSource) == (sizeof(void*) == 4 ? 40 : 64));
static_assert(offsetof(XpGlyphSource, font) == (sizeof(void*) == 4 ? 8 : 16));
static_assert(offsetof(XpGlyphSource, slots) == (sizeof(void*) == 4 ? 20 : 32));
static_assert(offsetof(XpGlyphSource, overflowFirst) == (sizeof(void*) == 4 ? 28 : 48));
static_assert(sizeof(XpGlyphPageSlot) == (sizeof(void*) == 4 ? 16 : 32));
static_assert(sizeof(XpGlyphBitmap) == (sizeof(void*) == 4 ? 8 : 16));

static_assert(sizeof(XpGlyphPaint) == (sizeof(void*) == 4 ? 104 : 136));
static_assert(sizeof(XpLoadedFonts) == 99 * sizeof(void*));
static_assert(sizeof(XpRegisteredFont) == (sizeof(void*) == 4 ? 24 : 48));
static_assert(sizeof(XpFontQueryResult) == sizeof(void*));
static_assert(sizeof(XpPageTarget) == (sizeof(void*) == 4 ? 64 : 80));
static_assert(offsetof(XpPageTarget, fonts) == sizeof(XpGlyphTarget));
static_assert(sizeof(XpPagePaint) == (sizeof(void*) == 4 ? 32 : 48));
static_assert(offsetof(XpPagePaint, plan) == 16);
