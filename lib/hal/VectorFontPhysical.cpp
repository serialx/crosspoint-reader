// This boundary is host-qualified before any shipping caller is switched.
#ifdef XP_VECTOR_OWNER_TEST
#include <FtFont.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <new>

#include "physical.h"

namespace {
struct Face {
  freeink::font::FtFont font;
  XpFtSource source;
};
static_assert(sizeof(Face) <= sizeof(XpFtSlot));
static_assert(alignof(Face) <= alignof(XpFtSlot));
Face& face(XpFtSlot* slot) { return *std::launder(reinterpret_cast<Face*>(slot)); }
unsigned long readSource(void* raw, unsigned long offset, unsigned char* out, unsigned long count) {
  const auto& source = *static_cast<const XpFtSource*>(raw);
  if (count == 0) return 0;
  unsigned long copied = 0;
  if (offset < source.length) {
    copied = count < source.length - offset ? count : source.length - offset;
    memcpy(out, source.data + offset, copied);
    if (copied == count) return count;
  }
  auto& file = *static_cast<HalFile*>(source.file);
  if (!file || !file.seek(offset + copied)) return copied;
  const int got = file.read(out + copied, count - copied);
  return copied + (got < 0 ? 0 : static_cast<unsigned long>(got));
}
}  // namespace
extern "C" bool xp_native_ft_open(XpFtSlot* slot, const XpFtSource* source, const XpFtConfig* config) {
  auto* f = new (slot) Face{};
  f->source = *source;
  const bool ok =
      source->mode == 1
          ? f->font.init(source->data, source->length, config->pixels, config->weight, config->italic)
          : f->font.initStream(readSource, &f->source, source->size, config->pixels, config->weight, config->italic);
  if (!ok) {
    f->~Face();
    return false;
  }
  freeink::font::FtFont::RenderOptions options;
  options.hinting =
      config->autoHint ? freeink::font::FtFont::HintingMode::Auto : freeink::font::FtFont::HintingMode::Native;
  options.stemDarkening = config->stemDarkening;
  if (!f->font.setRenderOptions(options))
    LOG_ERR("TTF", "Auto hinting unavailable (FREEINK_FONT_ENABLE_AUTOHINT not compiled)");
  f->font.setGposByteBudget(config->gposBudget);
  f->font.setGsubByteBudget(config->gsubBudget);
  return true;
}
extern "C" void xp_native_ft_close(XpFtSlot* slot) { face(slot).~Face(); }
extern "C" uint32_t xp_native_ft_glyph_id(XpFtSlot* slot, uint32_t cp) { return face(slot).font.glyphId(cp); }
extern "C" void xp_native_ft_raster(XpFtSlot* slot, uint32_t gid, uint32_t size, XpFtRaster* out) {
  *out = {};
  auto& ft = face(slot).font;
  freeink::font::FtFont::GlyphMetrics metrics;
  out->haveMetrics = ft.metricsGlyph26_6(gid, size, metrics);
  if (out->haveMetrics) out->metrics = metrics.advance26_6;
  if (const auto* glyph = ft.rasterizeGlyph26_6(gid, size)) {
    out->pixels = glyph->pixels;
    out->width = glyph->width;
    out->height = glyph->height;
    out->left = glyph->xoff;
    out->yoff = glyph->yoff;
    out->advance = glyph->advance;
  }
}
extern "C" int32_t xp_native_ft_kern(XpFtSlot* slot, uint32_t left, uint32_t right, uint32_t size) {
  return face(slot).font.kerning26_6(left, right, size);
}
extern "C" void xp_native_ft_line_metrics(XpFtSlot* slot, uint32_t size, uint16_t pixels, int32_t* ascent,
                                          int32_t* height) {
  auto& ft = face(slot).font;
  freeink::font::FtFont::LineMetrics metrics;
  if (ft.lineMetrics26_6(size, metrics)) {
    *ascent = metrics.ascender26_6;
    *height = metrics.height26_6;
  } else {
    *ascent = ft.ascent(pixels) * 64;
    *height = ft.lineHeight(pixels) * 64;
  }
}
extern "C" uint32_t xp_native_ft_ligature(XpFtSlot* slot, const uint32_t* cp, size_t count) {
  return face(slot).font.ligatureGlyphId(cp, count);
}
extern "C" void xp_native_ft_release_gsub(XpFtSlot* slot) { face(slot).font.releaseLigatureTable(); }
#endif
