#include "EpdFont.h"

#include <Utf8.h>
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
      nullptr,
      nullptr,
      nullptr,
      [](void* ctx, uint8_t) -> int32_t { return static_cast<EpdFont*>(ctx)->data->ascender; },
      nullptr,
      nullptr};
  int32_t width, height;
  xp_textm_dimensions_utf8(&face, reinterpret_cast<const uint8_t*>(text), strlen(text), &width, &height);
  *w = width;
  *h = height;
}

int8_t EpdFont::getKerning(const uint32_t left, const uint32_t right) const {
  int8_t result = 0;
  if (xp_font_kerning(data, left, right, &result)) return result;
  return data->kernHandler(data->glyphMissCtx, left, right);
}

uint32_t EpdFont::getLigature(uint32_t left, uint32_t right) const { return xp_font_ligature(data, left, right); }
uint32_t EpdFont::applyLigatures(uint32_t cp, const char*& text) const {
  size_t consumed = 0;
  const auto result =
      xp_font_apply_ligatures(data, cp, reinterpret_cast<const uint8_t*>(text), strlen(text), &consumed);
  text += consumed;
  return result;
}

const EpdGlyph* EpdFont::getGlyph(const uint32_t cp) const {
  if (data->intervalCount == 0 && !data->glyphMissHandler) return nullptr;
  if (const auto* glyph = static_cast<const EpdGlyph*>(xp_font_resident_glyph(data, cp))) return glyph;

  // Codepoint not in interval table — try on-demand loading (SD card fonts).
  if (data->glyphMissHandler) {
    const EpdGlyph* loaded = data->glyphMissHandler(data->glyphMissCtx, cp);
    if (loaded) return loaded;
  }

  // The turned comma uses the matching quotation-mark shape when absent.
  if (cp == 0x02BB && hasCodepoint(0x2018)) return getGlyph(0x2018);

  if (cp != REPLACEMENT_GLYPH) {
    return getGlyph(REPLACEMENT_GLYPH);
  }
  return nullptr;
}

bool EpdFont::hasCodepoint(const uint32_t cp) const {
  const int result = xp_font_has_codepoint(data, cp);
  return result >= 0 ? result != 0 : data->coverageHandler(data->glyphMissCtx, cp);
}

const EpdGlyph* EpdFont::getGlyphMetrics(const uint32_t cp, EpdGlyph& solidFallback) const {
  if (!syntheticGlyph::isSolid(cp) || hasCodepoint(cp)) return getGlyph(cp);

  // Match the current font's em width and ascender without allocating a bitmap.
  const EpdGlyph* emGlyph = hasCodepoint('M') ? getGlyph('M') : nullptr;
  xp_font_solid_metrics(cp, data->ascender, emGlyph ? emGlyph->advanceX : 0,
                        reinterpret_cast<XpFontGlyph*>(&solidFallback));
  return &solidFallback;
}
