#pragma once
#include <stddef.h>
#include <stdint.h>

#include "xp_ffi.h"
#ifdef __cplusplus
extern "C" {
#endif

// Serialized owner storage, initialized in place. No allocation or owned font bytes.
typedef struct {
  uintptr_t words[99];
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
uint8_t xp_font_registry_update(XpLoadedFonts*, XpFontUpdate, const XpRegisteredFont*);
// Copies one descriptor; BY_ID returns index+1, AT returns 1, absent returns 0.
// COUNT/EXISTS/FALLBACK_ID return scalars. SLOT returns a stable data-slot address,
// index selects style; these queries require no output descriptor.
XpFontQueryResult xp_font_registry_query(const XpLoadedFonts*, XpFontQuery, int32_t id, uint32_t index,
                                         XpRegisteredFont* out);
// Rust callbacks read this owner directly; native context is used only for the
// named temporary SD/cache operations. The provider is borrowed synchronously.
XpNativeProvider xp_font_registry_provider(const XpLoadedFonts*);
void xp_font_registry_source(const XpLoadedFonts*, const void* font, XpGlyphSource* out);

// Temporary font boundaries: cache view/bitmap/scan retire in PT2; SD table,
// residency and absent-font loading retire in PT3. No input is retained.
void xp_native_font_cache_view(void*, XpGlyphSource*);
XpGlyphBitmap xp_native_font_bitmap_miss(void*, const void* font, const void* glyph);
void xp_native_font_record_resolved_text(void*, int32_t font, uint8_t style, const uint8_t*);
void xp_native_sd_font_view(void*, int32_t font, XpNativeFamily*, XpGlyphSource*);
void xp_native_sd_font_resident_slice(void*, int32_t font, uint8_t style, const uint8_t*, size_t, uint8_t metadata);
uint8_t xp_native_sd_font_load(void*, int32_t font);
void xp_native_font_diagnostic(void*, uint32_t event, uint32_t value);
#ifdef __cplusplus
}
#endif
