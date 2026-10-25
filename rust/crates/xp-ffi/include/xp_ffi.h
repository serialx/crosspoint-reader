#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t xp_abi_version(void);
// Borrows len readable, unchanged bytes in one allocation; len <= PTRDIFF_MAX.
// data may be NULL only when len is zero. No allocation or retained pointer.
uint64_t xp_fnv1a64(const uint8_t* data, size_t len);
uint32_t xp_crc32(const uint8_t* data, size_t len);

// Hyphenation: borrowed inputs, caller-owned output; no allocation or retained pointers.
// Language identifiers are 0..10, or -1 when no language matches.
int32_t xp_hyphen_language(const uint8_t* tag, size_t len);

typedef struct {
  uint32_t byte_offset;
  uint8_t insert_hyphen;
  uint8_t pad[3];
} XpBreak;

// word/tag: len readable bytes in one allocation; NULL only for zero length.
// needed: one aligned writable size_t. out: capacity aligned writable XpBreak
// entries; NULL only for zero capacity. Input, output and needed must not overlap.
// Input remains unchanged during the call. len <= UINT32_MAX and PTRDIFF_MAX;
// capacity * sizeof(XpBreak) <= PTRDIFF_MAX. include_fallback must be 0 or 1.
// 0 OK: writes *needed entries. 1 NEED_OUTPUT: sets *needed, leaves out untouched.
// -1 INVALID: invalid scalar/null/alignment argument; outputs remain untouched.
// Other pointer validity and non-overlap requirements are caller obligations.
int32_t xp_hyphen_breaks(int32_t language, const uint8_t* word, size_t len, uint8_t include_fallback, XpBreak* out,
                         size_t capacity, size_t* needed);

// CSS v19. Handles are exclusively owned; no concurrent calls or retained input pointers.
#define XP_CSS_WIRE_BYTES 88
#define XP_CSS_CACHE_BUFFER_BYTES 347
typedef struct XpCss XpCss;
typedef struct {
  const uint8_t* data;
  size_t len;
} XpCssSlice;
typedef struct {
  uint32_t tag_hash;
  uint32_t id_hash;
  uint32_t class_hashes[4];
  uint8_t class_count;
} XpCssAncestor;
XpCss* xp_css_create(void);  // Null on allocation failure.
void xp_css_destroy(XpCss* handle);
void xp_css_clear(XpCss* handle);
// Input buffers borrow len readable bytes in one allocation, len <= PTRDIFF_MAX.
// NULL is allowed only for len == 0; output buffers must be writable and disjoint.
// feed returns 0 Complete, 1 Partial, 2 Error; end marks each stylesheet boundary.
uint8_t xp_css_feed(XpCss* handle, const uint8_t* data, size_t len, uint8_t end);
void xp_css_resolve(const XpCss* handle, XpCssSlice tag, XpCssSlice classes, XpCssSlice id,
                    const XpCssAncestor* ancestors, size_t count, uint8_t first_letter, uint8_t out[XP_CSS_WIRE_BYTES]);
void xp_css_inline(const uint8_t* data, size_t len, uint8_t out[XP_CSS_WIRE_BYTES]);
void xp_css_ancestor(XpCssSlice tag, XpCssSlice classes, XpCssSlice id, XpCssAncestor* out);
size_t xp_css_rule_count(const XpCss* handle);
uint8_t xp_css_has_first_letter(const XpCss* handle);
// Cache I/O returns 0 success, 1 LowMemory, 2 Invalid. Buffers and outputs must not overlap.
// order has rule_count entries; keep it unchanged until serialization ends.
int32_t xp_css_cache_write_begin(XpCss* handle, uint8_t complete, uint16_t* order, size_t count);
int32_t xp_css_cache_write(XpCss* handle, const uint16_t* order, size_t count, uint8_t* out, size_t cap,
                           size_t* written);
int32_t xp_css_cache_read(XpCss* handle, const uint8_t* data, size_t len, uint8_t end);

#ifdef __cplusplus
}
#endif
