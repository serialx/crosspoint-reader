#include "TtfEpdFont.h"

#if CROSSPOINT_VECTOR_FONTS

#include <Logging.h>
#include <MemoryManager.h>
#include <esp_heap_caps.h>

#include <cstring>

XpVectorServices TtfEpdFont::services(Face& f) {
  return {&f,
          [](void* ctx) { return &static_cast<Face*>(ctx)->state; },
          [](void* ctx) {
            auto& f = *static_cast<Face*>(ctx);
            f.owner->initFace(f);
          },
          [](void* ctx, uint8_t id) -> XpVectorBuffer {
            auto& f = *static_cast<Face*>(ctx);
            switch (static_cast<XpVectorBufferId>(id)) {
              case XpVectorBufferId::Bitmap:
                return {f.bmp.data(), f.bmp.size(), f.bmp.capacity()};
              case XpVectorBufferId::Glyphs:
                return {reinterpret_cast<uint8_t*>(f.glyphs.data()), f.glyphs.size(), f.glyphs.capacity()};
              case XpVectorBufferId::Codepoints:
                return {reinterpret_cast<uint8_t*>(f.cps.data()), f.cps.size(), f.cps.capacity()};
              case XpVectorBufferId::Slots:
                return {reinterpret_cast<uint8_t*>(f.slot.data()), f.slot.size(), f.slot.capacity()};
              case XpVectorBufferId::KernKeys:
                return {reinterpret_cast<uint8_t*>(f.kernKeys.data()), f.kernKeys.size(), f.kernKeys.capacity()};
              case XpVectorBufferId::KernValues:
                return {reinterpret_cast<uint8_t*>(f.kernVals.data()), f.kernVals.size(), f.kernVals.capacity()};
            }
            return {};
          },
          [](void* ctx, uint8_t id, size_t count) {
            auto& f = *static_cast<Face*>(ctx);
            switch (static_cast<XpVectorBufferId>(id)) {
              case XpVectorBufferId::Bitmap:
                f.bmp.reserve(count);
                break;
              case XpVectorBufferId::Glyphs:
                f.glyphs.reserve(count);
                break;
              case XpVectorBufferId::Codepoints:
                f.cps.reserve(count);
                break;
              case XpVectorBufferId::Slots:
                f.slot.reserve(count);
                break;
              case XpVectorBufferId::KernKeys:
                f.kernKeys.reserve(count);
                break;
              case XpVectorBufferId::KernValues:
                f.kernVals.reserve(count);
                break;
            }
          },
          [](void* ctx, uint8_t id, size_t count) {
            auto& f = *static_cast<Face*>(ctx);
            switch (static_cast<XpVectorBufferId>(id)) {
              case XpVectorBufferId::Bitmap:
                f.bmp.resize(count, 0);
                break;
              case XpVectorBufferId::Glyphs:
                f.glyphs.clear();
                break;
              case XpVectorBufferId::Codepoints:
                f.cps.clear();
                break;
              case XpVectorBufferId::Slots:
                f.slot.clear();
                break;
              case XpVectorBufferId::KernKeys:
                f.kernKeys.clear();
                break;
              case XpVectorBufferId::KernValues:
                f.kernVals.clear();
                break;
            }
          },
          [](void* ctx, uint8_t id, size_t pos, const uint8_t* value) {
            auto& f = *static_cast<Face*>(ctx);
            switch (static_cast<XpVectorBufferId>(id)) {
              case XpVectorBufferId::Bitmap:
                break;
              case XpVectorBufferId::Glyphs:
                f.glyphs.push_back(*reinterpret_cast<const EpdGlyph*>(value));
                break;
              case XpVectorBufferId::Codepoints:
                f.cps.insert(f.cps.begin() + pos, *reinterpret_cast<const uint32_t*>(value));
                break;
              case XpVectorBufferId::Slots:
                f.slot.insert(f.slot.begin() + pos, *reinterpret_cast<const uint16_t*>(value));
                break;
              case XpVectorBufferId::KernKeys:
                f.kernKeys.insert(f.kernKeys.begin() + pos, *reinterpret_cast<const uint64_t*>(value));
                break;
              case XpVectorBufferId::KernValues:
                f.kernVals.insert(f.kernVals.begin() + pos, *reinterpret_cast<const int8_t*>(value));
                break;
            }
          },
          [](void* ctx, uint8_t id) {
            auto& f = *static_cast<Face*>(ctx);
            switch (static_cast<XpVectorBufferId>(id)) {
              case XpVectorBufferId::Bitmap:
                freeink::font::PsramVector<uint8_t>().swap(f.bmp);
                break;
              case XpVectorBufferId::Glyphs:
                freeink::font::PsramVector<EpdGlyph>().swap(f.glyphs);
                break;
              case XpVectorBufferId::Codepoints:
                freeink::font::PsramVector<uint32_t>().swap(f.cps);
                break;
              case XpVectorBufferId::Slots:
                freeink::font::PsramVector<uint16_t>().swap(f.slot);
                break;
              case XpVectorBufferId::KernKeys:
                freeink::font::PsramVector<uint64_t>().swap(f.kernKeys);
                break;
              case XpVectorBufferId::KernValues:
                freeink::font::PsramVector<int8_t>().swap(f.kernVals);
                break;
            }
          },
          [](uint8_t metric) -> size_t {
            switch (static_cast<XpVectorHeapMetric>(metric)) {
              case XpVectorHeapMetric::LargestPsram:
                return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
              case XpVectorHeapMetric::LargestInternal:
                return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
              case XpVectorHeapMetric::FreeInternal:
                return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
            return 0;
          },
          [](void* ctx) {
            auto& owner = *static_cast<Face*>(ctx)->owner;
            owner.evictionLocked_ = true;
            freeink::MemoryManager::instance().ensureFree(16 * 1024);
            owner.evictionLocked_ = false;
          },
          [](void* ctx, uint32_t cp) { return static_cast<Face*>(ctx)->ft.glyphId(cp); },
          [](void* ctx, uint32_t gid, XpVectorRaster* out) {
            auto& f = *static_cast<Face*>(ctx);
            freeink::font::FtFont::GlyphMetrics metrics;
            out->haveMetrics = f.ft.metricsGlyph26_6(gid, f.owner->size26_6_, metrics);
            if (out->haveMetrics) out->metrics = metrics.advance26_6;
            const auto* g = f.ft.rasterizeGlyph26_6(gid, f.owner->size26_6_);
            if (!g) return;
            out->pixels = g->pixels;
            out->width = g->width;
            out->height = g->height;
            out->left = g->xoff;
            out->yoff = g->yoff;
            out->advance = g->advance;
          },
          [](void* ctx, uint32_t left, uint32_t right) {
            auto& f = *static_cast<Face*>(ctx);
            return f.ft.kerning26_6(left, right, f.owner->size26_6_);
          },
          [](void* ctx, const XpVectorSource* source, uint16_t size, int32_t weight, bool italic) {
            auto& ft = static_cast<Face*>(ctx)->ft;
            return source->streamed ? ft.initStream(source->read, source->ctx, source->fileSize, size, weight, italic)
                                    : ft.init(source->data, source->len, size, weight, italic);
          },
          [](void* ctx) {
            freeink::font::FtFont::RenderOptions options;
            options.hinting = freeink::font::FtFont::HintingMode::Auto;
            options.stemDarkening = true;
            if (!static_cast<Face*>(ctx)->ft.setRenderOptions(options)) {
              LOG_ERR("TTF", "Auto hinting unavailable (FREEINK_FONT_ENABLE_AUTOHINT not compiled)");
            }
          },
          [](void* ctx, size_t bytes) { static_cast<Face*>(ctx)->ft.setGposByteBudget(bytes); },
          [](void* ctx, size_t bytes) { static_cast<Face*>(ctx)->ft.setGsubByteBudget(bytes); },
          [](void* ctx, const uint32_t* components, size_t count) {
            return static_cast<Face*>(ctx)->ft.ligatureGlyphId(components, count);
          },
          [](void* ctx) { static_cast<Face*>(ctx)->ft.releaseLigatureTable(); },
          [](void* ctx, uint32_t cp) { return static_cast<Face*>(ctx)->ft.hasGlyph(cp); },
          [](void* ctx) { static_cast<Face*>(ctx)->ft.deinit(); },
          [](void* ctx, bool regular, int32_t* ascent, int32_t* height) {
            auto& f = *static_cast<Face*>(ctx);
            auto& ft = regular ? f.owner->faces_[0].ft : f.ft;
            freeink::font::FtFont::LineMetrics metrics;
            if (ft.lineMetrics26_6(f.owner->size26_6_, metrics)) {
              *ascent = (metrics.ascender26_6 + 32) >> 6;
              *height = (metrics.height26_6 + 32) >> 6;
            } else {
              *ascent = ft.ascent(f.state.sizePx);
              *height = ft.lineHeight(f.state.sizePx);
            }
          }};
}

void TtfEpdFont::addResidentSource(const uint8_t style, const uint8_t* data, const uint32_t len) {
  if (style >= 4 || data == nullptr || len == 0) return;
  Source& s = sources_[style];
  s = Source{};
  s.present = true;
  s.streamed = false;
  s.data = data;
  s.len = len;
}

void TtfEpdFont::addStreamSource(const uint8_t style, const freeink::font::FtFont::ReadFn read, void* ctx,
                                 const unsigned long fileSize) {
  if (style >= 4 || read == nullptr || fileSize == 0) return;
  Source& s = sources_[style];
  s = Source{};
  s.present = true;
  s.streamed = true;
  s.read = read;
  s.ctx = ctx;
  s.fileSize = fileSize;
}

XpVectorFamily TtfEpdFont::familyServices() {
  return {this,
          sources_,
          &size26_6_,
          &loaded_,
          &evictionLocked_,
          [](void* ctx, uint8_t index) {
            auto& self = *static_cast<TtfEpdFont*>(ctx);
            auto& f = self.faces_[index];
            f.owner = &self;
            return services(f);
          },
          [](void* ctx) {
            auto& f = *static_cast<Face*>(ctx);
            f.owner->setupFace(f);
          }};
}

bool TtfEpdFont::load(uint16_t pointSize, bool twoBit, size_t glyphCacheBytes, uint16_t maxGlyphs) {
  const auto io = familyServices();
  return xp_vector_load(&io, pointSize, twoBit, glyphCacheBytes, maxGlyphs);
}
void TtfEpdFont::initFace(Face& f) {
  const auto io = services(f);
  if (f.state.inited) return;
  xp_vector_init(&io, sources_);
  setupFace(f);
}
void TtfEpdFont::setupFace(Face& f) {
  const auto io = services(f);
  xp_vector_metrics(&io, &f.data);
  f.data.glyphMissHandler = &TtfEpdFont::missThunk;
  f.data.glyphMissCtx = &f;
  f.data.coverageHandler = &TtfEpdFont::coverageThunk;
  f.data.vectorBitmapHandler = &TtfEpdFont::bitmapThunk;
  f.data.kernHandler = &TtfEpdFont::kernThunk;
}
void TtfEpdFont::clearCache() {
  const auto io = familyServices();
  xp_vector_clear(&io, false);
}
void TtfEpdFont::releaseResidentCaches() {
  const auto io = familyServices();
  xp_vector_clear(&io, true);
}

const EpdGlyph* TtfEpdFont::faultGlyph(Face& f, uint32_t cp) {
  const auto io = services(f);
  return static_cast<const EpdGlyph*>(xp_vector_glyph(&io, cp));
}
int8_t TtfEpdFont::faultKern(Face& f, uint32_t leftCp, uint32_t rightCp) {
  const auto io = services(f);
  return xp_vector_kern(&io, leftCp, rightCp);
}

const EpdGlyph* TtfEpdFont::missThunk(void* ctx, const uint32_t codepoint) {
  Face* f = static_cast<Face*>(ctx);
  return f->owner->faultGlyph(*f, codepoint);
}
int8_t TtfEpdFont::kernThunk(void* ctx, const uint32_t leftCp, const uint32_t rightCp) {
  Face* f = static_cast<Face*>(ctx);
  return f->owner->faultKern(*f, leftCp, rightCp);
}
const uint8_t* TtfEpdFont::bitmapThunk(void* ctx, const EpdGlyph* glyph) {
  if (glyph == nullptr || glyph->dataLength == 0) return nullptr;
  return static_cast<Face*>(ctx)->bmp.data() + glyph->dataOffset;
}
bool TtfEpdFont::coverageThunk(void* ctx, uint32_t cp) {
  auto& regular = static_cast<Face*>(ctx)->owner->faces_[0];
  const auto io = services(regular);
  return xp_vector_coverage(&io, cp);
}

EpdFontFamily TtfEpdFont::family() const {
  return EpdFontFamily(&faces_[0].font, &faces_[1].font, &faces_[2].font, &faces_[3].font);
}

bool TtfEpdFont::build(const char* utf8) {
  const auto io = familyServices();
  return xp_vector_prewarm(&io, reinterpret_cast<const uint8_t*>(utf8), utf8 ? strlen(utf8) : 0, true);
}
bool TtfEpdFont::addCoverage(const char* utf8) {
  const auto io = familyServices();
  return xp_vector_prewarm(&io, reinterpret_cast<const uint8_t*>(utf8), utf8 ? strlen(utf8) : 0, false);
}

#endif  // CROSSPOINT_VECTOR_FONTS
