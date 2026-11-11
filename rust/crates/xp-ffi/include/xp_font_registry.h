#pragma once
#include <stddef.h>
#include <stdint.h>

#include "xp_ffi.h"
#ifdef __cplusplus
extern "C" {
#endif
// Inline registry, cache, decoder, scan and catalog storage; legacy hosts omit the catalog.
// Cache buffers use the font allocator; owner initialization allocates nothing.
typedef struct {
#ifdef XP_SD_FONT_CATALOG
  uintptr_t words[sizeof(void*) == 8 ? 621 : 1068];
#else
  uintptr_t words[sizeof(void*) == 8 ? 616 : 1063];
#endif
} XpLoadedFonts;
typedef struct {
  int32_t id;
  const void* const* slots[4];
  int32_t fallback;
} XpRegisteredFont;
typedef enum {
  XP_FONT_REGISTER,
  XP_FONT_REMOVE,
  XP_FONT_FALLBACK,
  XP_FONT_CLEAR_FALLBACKS
#ifdef XP_SD_FONT_CATALOG
  ,
  XP_FONT_ENSURE_LOADED
#endif
} XpFontUpdate;
typedef enum {
  XP_FONT_BY_ID,
  XP_FONT_AT,
  XP_FONT_COUNT,
  XP_FONT_SLOT,
  XP_FONT_EXISTS,
  XP_FONT_FALLBACK_ID,
  XP_FONT_SD
} XpFontQuery;
typedef union {
  uintptr_t value;
  const void* const* slot;
} XpFontQueryResult;
// Generation and native context outlive the owner. Each nonnull slot is a stable,
// aligned FontData pointer cell, live until unregister. No loans cross mutation.
// Initialization is serialized per registry. Every operation takes its explicit owner;
// registries do not bind a process singleton.
void xp_font_registry_init(XpLoadedFonts*, const void* generation, void* native);
void xp_font_registry_drop(XpLoadedFonts*);
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
// Native-origin cache scopes borrow their explicit Rust owner synchronously.
// Begin starts scanning; finish prewarms; end finishes if needed and clears frame caches.
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
// operation is an XpFontRelease value. CLEAR_* return 0; EVICT_* report default-heap bytes freed.
// CLEAR_RESIDENT also releases SD mini/kerning/ligature/overflow/advance tables and vector arenas.
// Faces remain registered; cache data reloads on demand.
size_t xp_font_cache_release(const XpLoadedFonts*, uint8_t operation);
void xp_font_prewarm_text(const XpLoadedFonts*, int32_t font, const char* text, uint8_t styles, uint8_t accumulate);
void xp_font_record_text(const XpLoadedFonts*, int32_t font, const char* text, uint8_t style);
uint8_t xp_font_scanning(const XpLoadedFonts*);
typedef struct {
  const XpLoadedFonts* owner;
  const void* const* slot;
} XpFontGlyphContext;
XpGlyphBitmap xp_font_glyph_miss(void*, uint32_t cp, const void* glyph, XpGlyphDraw*, XpGlyphSource*);
int32_t xp_sd_font_prewarm(const XpLoadedFonts*, int32_t, const char* (*)(const void*, uint32_t), const void*,
                           uint32_t count, uint8_t styles, uint8_t metadata, uint8_t kern, uint8_t accumulate);
int32_t xp_sd_font_prepare(const XpLoadedFonts*, int32_t, const XpSdfontTexts*, uint8_t styles);
void xp_sd_font_prepare_spans(void*, int32_t, const XpParaSpan*, uint32_t, uint8_t space, uint8_t hyphen,
                              uint8_t styles, uint8_t small);
// Native chapter sink context still belongs to ChapterPageAssembler. This leaf
// only lends its explicit owner address; P18-D2 retires it with that context.
const XpLoadedFonts* xp_native_chapter_font_owner(void*);
void xp_sd_font_prepare_chapter(void*, int32_t, const XpParaSpan*, uint32_t, uint8_t space, uint8_t hyphen,
                                uint8_t styles, uint8_t small);
uint8_t xp_sd_font_take_corrupt_notice(void);
#ifdef XP_SD_FONT_CATALOG
typedef struct {
  const char* name;
  uint8_t pointSize, style;
} XpSdFile;
typedef enum {
  XP_SD_COUNT,
  XP_SD_AT,
  XP_SD_FIND,
  XP_SD_PATH,
  XP_SD_ROOT,
  XP_SD_WRITE_ROOT,
  XP_SD_LOADED_ID,
  XP_SD_SELECTABLE_SIZES
} XpSdCatalogOperation;
typedef struct {
  uint32_t operation;
  size_t index, file;
  const uint8_t* name;
  size_t length;
  uint8_t* output;
  size_t capacity;
} XpSdCatalogQuery;
typedef struct {
  const char* name;
  size_t length;
  const char* root;
  const XpSdFile* files;
  size_t count;
  uint32_t sizes[8];
  int32_t id;
  bool vector, loose;
} XpSdCatalogView;
// Family begins with the existing repr(C) small-string record. These strided
// records and heap names are borrowed until catalog mutation/drop.
typedef struct {
  union {
    uint8_t inline_bytes[16];
    const char* heap;
  } text;
  size_t length, capacity;
} XpSdName;
typedef struct {
  const XpLoadedFonts* owner;
  const uint8_t* family;
  size_t length;
  const void* names;
  size_t count, stride;
  uint32_t sizes[8];
  size_t reserve;
} XpSdChoices;
#ifdef __cplusplus
static_assert(sizeof(XpSdName) == (sizeof(void*) == 8 ? 32 : 24));
static_assert(sizeof(XpSdChoices) == (sizeof(void*) == 8 ? 88 : 60));
static_assert(offsetof(XpSdChoices, names) == 3 * sizeof(void*));
#endif
// SELECTABLE_SIZES fills sizes and returns native initial capacity (zero means
// incremental growth). ROOT/WRITE_ROOT and size queries allow a null owner.
// COUNT returns count; AT/FIND return index+1; PATH returns success; ROOT/WRITE_ROOT
// return a static root address. Views borrow names/files until rediscovery/drop.
uintptr_t xp_sd_catalog_query(const XpLoadedFonts*, const XpSdCatalogQuery*, XpSdCatalogView*);
typedef enum { XP_SD_MARK_DIRTY, XP_SD_REFRESH_DIRTY, XP_SD_DISCOVER } XpSdCatalogUpdate;
// Mark is concurrent-safe; refresh/discover are serialized and report rediscovery.
bool xp_sd_catalog_dirty(const XpLoadedFonts*, uint8_t operation, bool vectors);
typedef struct {
  char* family;
  uint8_t* points;
  int32_t ui[3];
  bool vectors, vector_loaded;
  XpSdCatalogView vector_family;
  size_t vector_index;
} XpSdSettings;
typedef struct XpSdContinuation {
  const struct XpSdContinuation* address;
  const XpLoadedFonts* owner;
  const XpSdSettings* request;
  uint32_t epoch, step;
  bool dirty, boot;
} XpSdContinuation;
typedef enum {
  XP_SD_COMPLETE,
  XP_SD_PERSIST_SETTINGS,
  XP_SD_UNLOAD_VECTOR,
  XP_SD_LOAD_VECTOR,
  XP_SD_REJECTED,
  XP_SD_PREPARE_VECTOR,
  XP_SD_BORROW_SETTINGS
} XpSdAction;
// Zero token starts ensure. Resume uses the same owner/settings/token storage.
// Effects execute after return. Vector unregister effects acknowledge the current
// owner generation in epoch; no reference or source loan crosses an effect.
XpSdAction xp_sd_fonts_begin(const XpLoadedFonts*, XpSdSettings*, XpSdContinuation*);
XpSdAction xp_sd_fonts_ensure(const XpLoadedFonts*, XpSdSettings*, XpSdContinuation*);
int32_t xp_sd_font_variant(const XpLoadedFonts*, int32_t, float scale);
#endif
// Vector compatibility remains; SD policy uses the Rust owner directly.
int32_t xp_native_font_compat_prewarm(void*, int32_t font, uint8_t backend, const char* text, uint8_t styles,
                                      uint8_t metadata, uint8_t kern_lig, uint8_t accumulate);
void xp_native_font_compat_clear(void*, int32_t font, uint8_t backend, uint8_t resident);

void xp_native_font_cache_diagnostic(const void* font, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
// Legacy hosts retain native absent-ID loading; shipping native providers serve PT-VEC only.
#if !defined(XP_SD_FONT_CATALOG) || defined(BOARD_HAS_PSRAM) || defined(XP_APP_TEST)
uint8_t xp_native_sd_font_load(void*, int32_t font);
#endif
void xp_native_font_diagnostic(void*, uint32_t event, uint32_t value);
#ifdef __cplusplus
}
#endif

#ifdef XP_APP_TEST
#include <xp_sd_owner_oracle.h>
#endif
