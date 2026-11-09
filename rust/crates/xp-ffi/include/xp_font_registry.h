#pragma once
#include <stddef.h>
#include <stdint.h>

#include "xp_ffi.h"
#ifdef __cplusplus
extern "C" {
#endif
// Inline registry, cache, decoder and scan storage: 4,000 B on C3, 4,568 B on host.
// Cache buffers use the font allocator; owner initialization allocates nothing.
typedef struct {
  uintptr_t words[sizeof(void*) == 8 ? 571 : 1000];
} XpLoadedFonts;
typedef struct {
  int32_t id;
  const void* const* slots[4];
  int32_t fallback;
} XpRegisteredFont;
typedef enum { XP_FONT_REGISTER, XP_FONT_REMOVE, XP_FONT_FALLBACK, XP_FONT_CLEAR_FALLBACKS } XpFontUpdate;
typedef enum {
  XP_FONT_BY_ID,
  XP_FONT_AT,
  XP_FONT_COUNT,
  XP_FONT_SLOT,
  XP_FONT_EXISTS,
  XP_FONT_FALLBACK_ID
} XpFontQuery;
typedef union {
  uintptr_t value;
  const void* const* slot;
} XpFontQueryResult;
// Generation and native context outlive the owner. Each nonnull slot is a stable,
// aligned FontData pointer cell, live until unregister. No loans cross mutation.
void xp_font_registry_init(XpLoadedFonts*, const void* generation, void* native);
// Applies registration/fallback changes and invalidates source loans; owns no font bytes.
uint8_t xp_font_registry_update(XpLoadedFonts*, XpFontUpdate, const XpRegisteredFont*);
// Copies one descriptor; BY_ID returns index+1, AT returns 1, absent returns 0.
// COUNT/EXISTS/FALLBACK_ID return scalars. SLOT returns a stable data-slot address,
// index selects style; these queries require no output descriptor.
XpFontQueryResult xp_font_registry_query(const XpLoadedFonts*, XpFontQuery, int32_t id, uint32_t index,
                                         XpRegisteredFont* out);
// Rust callbacks read this owner directly; native context is used only for the
// named temporary SD/cache operations. The provider is borrowed synchronously.
XpNativeProvider xp_font_registry_provider(const XpLoadedFonts*);
// Fills a synchronous glyph-source loan from a registered FontData and the current generation.
void xp_font_registry_source(const XpLoadedFonts*, const void* font, XpGlyphSource* out);
// Native-origin cache scopes borrow the single Rust owner synchronously.
void xp_font_frame_begin(const XpLoadedFonts*);
void xp_font_frame_finish(const XpLoadedFonts*);
void xp_font_frame_end(const XpLoadedFonts*);
typedef enum {
  XP_FONT_CLEAR_FRAME,
  XP_FONT_CLEAR_RESIDENT,
  XP_FONT_EVICT_RENDER,
  XP_FONT_EVICT_SD,
  XP_FONT_EVICT_VECTOR
} XpFontRelease;
size_t xp_font_cache_release(const XpLoadedFonts*, uint8_t operation);
void xp_font_prewarm_text(const XpLoadedFonts*, int32_t font, const char* text, uint8_t styles, uint8_t accumulate);
void xp_font_record_text(const XpLoadedFonts*, int32_t font, const char* text, uint8_t style);
uint8_t xp_font_scanning(const XpLoadedFonts*);
typedef struct {
  const XpLoadedFonts* owner;
  const void* const* slot;
} XpFontGlyphContext;
XpGlyphBitmap xp_font_glyph_miss(void*, uint32_t cp, const void* glyph, XpGlyphDraw*, XpGlyphSource*);
// Temporary four-operation compat: Rust selects backend, ordering and flags.
// PT3 removes SD; PT-VEC removes vector and native xp_vector policy routing.
uint8_t xp_native_font_compat_resolve(void*, int32_t font, uint8_t style);
int32_t xp_native_font_compat_prewarm(void*, int32_t font, uint8_t backend, const char* text, uint8_t styles,
                                      uint8_t metadata, uint8_t kern_lig, uint8_t accumulate);
void xp_native_font_compat_clear(void*, int32_t font, uint8_t backend, uint8_t resident);
void xp_native_font_compat_reset(void*, int32_t font);
void xp_native_font_cache_diagnostic(const void* font, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
// PT3 removes the per-glyph SD bitmap leaf with its native overflow/cache owner.
const uint8_t* xp_native_sd_font_bitmap(const void* font, const void* glyph);
// PT3 removes this leaf when Rust owns mini/advance/overflow tables.
void xp_native_sd_font_view(void*, int32_t font, XpNativeFamily*, XpGlyphSource*);
// PT3 removes this leaf when Rust residency uses physical file reads directly.
void xp_native_sd_font_resident_slice(void*, int32_t font, uint8_t style, const uint8_t*, size_t, uint8_t metadata);
// PT3 removes this leaf when Rust owns SD loading, registration and eviction together.
uint8_t xp_native_sd_font_load(void*, int32_t font);
void xp_native_font_diagnostic(void*, uint32_t event, uint32_t value);
#ifdef __cplusplus
}
#endif
