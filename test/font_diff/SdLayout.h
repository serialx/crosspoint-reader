// Test-only checks for the source and borrowed SD view layouts.
static_assert(sizeof(SdCardFont::AdvanceEntry) == 8);
static_assert(alignof(SdCardFont::AdvanceEntry) == 4);
static_assert(offsetof(SdCardFont::AdvanceEntry, codepoint) == 0);
static_assert(offsetof(SdCardFont::AdvanceEntry, advanceX) == 4);
static_assert(sizeof(SdCardFont::PerStyle::BmpInterval16) == 6);
static_assert(alignof(SdCardFont::PerStyle::BmpInterval16) == 1);
static_assert(offsetof(SdCardFont::PerStyle::BmpInterval16, first) == 0);
static_assert(offsetof(SdCardFont::PerStyle::BmpInterval16, last) == 2);
static_assert(offsetof(SdCardFont::PerStyle::BmpInterval16, offset) == 4);

static_assert(sizeof(XpFontGlyph) == sizeof(EpdGlyph));
static_assert(alignof(XpFontGlyph) == alignof(EpdGlyph));
static_assert(offsetof(XpFontGlyph, width) == offsetof(EpdGlyph, width));
static_assert(offsetof(XpFontGlyph, height) == offsetof(EpdGlyph, height));
static_assert(offsetof(XpFontGlyph, advanceX) == offsetof(EpdGlyph, advanceX));
static_assert(offsetof(XpFontGlyph, left) == offsetof(EpdGlyph, left));
static_assert(offsetof(XpFontGlyph, top) == offsetof(EpdGlyph, top));
static_assert(offsetof(XpFontGlyph, dataLength) == offsetof(EpdGlyph, dataLength));
static_assert(offsetof(XpFontGlyph, dataOffset) == offsetof(EpdGlyph, dataOffset));
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(XpFontSdView) == 24);
static_assert(alignof(XpFontSdView) == 4);
static_assert(offsetof(XpFontSdView, data) == 0);
static_assert(offsetof(XpFontSdView, advances) == 4);
static_assert(offsetof(XpFontSdView, advanceCount) == 8);
static_assert(offsetof(XpFontSdView, fullIntervals) == 12);
static_assert(offsetof(XpFontSdView, bmpIntervals) == 16);
static_assert(offsetof(XpFontSdView, coverageCount) == 20);
#endif
#if UINTPTR_MAX == UINT64_MAX
static_assert(sizeof(XpFontSdView) == 48);
static_assert(alignof(XpFontSdView) == 8);
static_assert(offsetof(XpFontSdView, data) == 0);
static_assert(offsetof(XpFontSdView, advances) == 8);
static_assert(offsetof(XpFontSdView, advanceCount) == 16);
static_assert(offsetof(XpFontSdView, fullIntervals) == 24);
static_assert(offsetof(XpFontSdView, bmpIntervals) == 32);
static_assert(offsetof(XpFontSdView, coverageCount) == 40);
#endif
