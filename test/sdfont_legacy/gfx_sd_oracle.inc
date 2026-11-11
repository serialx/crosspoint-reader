// Historical native SD adapters for renderer compatibility fixtures.
extern "C" void xp_native_sd_font_view(void* raw, int32_t font, XpNativeFamily* family, XpGlyphSource* source) {
  if (family) {
    const auto& fonts = static_cast<const GfxRenderer*>(raw)->getSdCardFonts();
    const auto found = fonts.find(font);
    if (found != fonts.end()) found->second->metricViews(*family);
  }
  if (source && source->font) {
    const auto* data = static_cast<const EpdFontData*>(source->font);
    if (data->glyphMissCtx) SdCardFont::fromMissCtx(data->glyphMissCtx)->bitmapView(data, *source);
  }
}
extern "C" void xp_native_sd_font_resident_slice(void* raw, int32_t font, uint8_t style, const uint8_t* text,
                                                 size_t length, uint8_t metadata) {
  const auto& fonts = static_cast<const GfxRenderer*>(raw)->getSdCardFonts();
  const auto found = fonts.find(font);
  if (found == fonts.end()) return;
  found->second->prewarmSpan(text, length, static_cast<uint8_t>(1u << (style & 3)), metadata != 0);
}
extern "C" uint8_t xp_native_font_compat_resolve(void* raw, int32_t font, uint8_t style) {
  const auto& fonts = static_cast<const GfxRenderer*>(raw)->getSdCardFonts();
  const auto it = fonts.find(font);
  return it != fonts.end() ? it->second->resolveStyle(style) : style;
}
extern "C" void xp_native_font_compat_reset(void* raw, int32_t font) {
  const auto& fonts = static_cast<const GfxRenderer*>(raw)->getSdCardFonts();
  const auto it = fonts.find(font);
  if (it != fonts.end()) it->second->resetStats();
}
extern "C" const uint8_t* xp_native_sd_font_bitmap(const void* rawFont, const void* rawGlyph) {
  const auto* data = static_cast<const EpdFontData*>(rawFont);
  const auto* glyph = static_cast<const EpdGlyph*>(rawGlyph);
  const auto* font = SdCardFont::fromMissCtx(data->glyphMissCtx);
  return font->isOverflowGlyph(glyph) ? font->getOverflowBitmap(glyph) : data->bitmap + glyph->dataOffset;
}
