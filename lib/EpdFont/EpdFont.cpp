#include "EpdFont.h"

#include <xp_ffi.h>

#include <cstring>

void EpdFont::getTextDimensions(const char* text, int* w, int* h) const {
  const XpTextmFace face{
      const_cast<EpdFont*>(this),
      [](void* ctx, uint32_t cp, uint8_t, XpTextmGlyph* out) -> uint8_t {
        const auto* glyph = static_cast<EpdFont*>(ctx)->getGlyph(cp);
        if (!glyph) return false;
        *out = {glyph->advanceX, glyph->width, glyph->height, glyph->left, glyph->top};
        return true;
      },
      [](void* ctx, uint32_t cp, uint8_t) -> uint8_t { return static_cast<EpdFont*>(ctx)->hasCodepoint(cp); },
      [](void* ctx, uint32_t left, uint32_t right, uint8_t) -> int8_t {
        return static_cast<EpdFont*>(ctx)->getKerning(left, right);
      },
      [](void* ctx, uint32_t left, uint32_t right, uint8_t) -> uint32_t {
        return static_cast<EpdFont*>(ctx)->getLigature(left, right);
      },
      [](void*) -> uint8_t { return false; },
      [](void*, uint8_t) -> uint8_t { return 0; },
      [](void*, uint32_t, uint8_t) -> uint16_t { return 0; },
      [](void* ctx, uint8_t) -> int32_t { return static_cast<EpdFont*>(ctx)->data->ascender; },
      [](void* ctx) -> uint8_t { return static_cast<EpdFont*>(ctx)->data->advanceY; },
      xp_bidi_transparent_mark};
  int32_t width, height;
  xp_textm_dimensions_utf8(&face, reinterpret_cast<const uint8_t*>(text), strlen(text), &width, &height);
  *w = width;
  *h = height;
}

int8_t EpdFont::getKerning(uint32_t left, uint32_t right) const { return xp_font_dynamic_kern(data, left, right); }

uint32_t EpdFont::getLigature(uint32_t left, uint32_t right) const { return xp_font_ligature(data, left, right); }
uint32_t EpdFont::applyLigatures(uint32_t cp, const char*& text) const {
  size_t consumed = 0;
  const auto result =
      xp_font_apply_ligatures(data, cp, reinterpret_cast<const uint8_t*>(text), strlen(text), &consumed);
  text += consumed;
  return result;
}

const EpdGlyph* EpdFont::getGlyph(uint32_t cp) const { return static_cast<const EpdGlyph*>(xp_font_lookup(data, cp)); }
bool EpdFont::hasCodepoint(uint32_t cp) const { return xp_font_coverage(data, cp); }
const EpdGlyph* EpdFont::getGlyphMetrics(uint32_t cp, EpdGlyph& solidFallback) const {
  return static_cast<const EpdGlyph*>(xp_font_metrics(data, cp, reinterpret_cast<XpFontGlyph*>(&solidFallback)));
}
