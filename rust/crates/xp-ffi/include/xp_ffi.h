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

// ZIP32 directory: caller owns all storage and performs absolute HalFile reads.
// No allocation or retained pointers. Inputs/outputs must be valid, aligned,
// disjoint, and exclusively borrowed during each call; byte lengths <= PTRDIFF_MAX.
// NULL byte pointers are accepted only for zero length. Struct outputs are required.
typedef struct {
  uint32_t offset, length;
} XpZipRead;
typedef struct {
  uint32_t central_offset;
  uint16_t entries;
} XpZipDetails;
typedef struct {
  XpZipRead name;
  uint32_t next_offset, crc32, compressed_size, uncompressed_size, local_offset;
  uint16_t method;
} XpZipEntry;
// Opaque fields: initialize with xp_zip_begin; do not modify or concurrently use.
typedef struct {
  uint32_t opaque[4];
} XpZipWalker;
// Request/parse functions: 0 success, -1 invalid/truncated input; outputs unchanged on failure.
int32_t xp_zip_eocd_request(uint32_t size, XpZipRead* out);
int32_t xp_zip_eocd(const uint8_t* data, size_t len, XpZipDetails* out);
int32_t xp_zip_begin(uint32_t central, uint32_t start, XpZipWalker* out);
// request: 1 read header, 0 finished, -1 invalid arguments.
int32_t xp_zip_request(const XpZipWalker* walker, XpZipRead* out);
// next: 1 entry (name read request in out), 2 wrapped (request again), 0 end,
// -1 invalid arguments or offset overflow. Supply the reply to the last request;
// short replies end/wrap the scan. A name length >= 256 must be skipped.
int32_t xp_zip_next(XpZipWalker* walker, const uint8_t* data, size_t len, XpZipEntry* out);
int32_t xp_zip_local_request(uint32_t offset, XpZipRead* out);
int32_t xp_zip_data_offset(uint32_t offset, const uint8_t* data, size_t len, uint64_t* out);
// 1 exact byte match, 0 mismatch/oversized name, -1 invalid arguments.
int32_t xp_zip_name_matches(const uint8_t* name, size_t len, const uint8_t* target, size_t target_len);

// Paragraph word storage. No allocation for the opaque store itself.
// State storage is caller-owned: four pointer-sized words aligned to sizeof(void*).
// Allocator callbacks return pointer-aligned memory or NULL; release matches allocate.
// The allocator record and callbacks outlive the store. No callback may reenter it.
// All calls are externally serialized. destroy is required exactly once after init.
typedef struct XpWordStore XpWordStore;
typedef struct {
  void* (*allocate)(size_t bytes);
  void (*release)(void* data);
} XpParaAllocator;
typedef struct {
  uint32_t chunk;
  uint16_t off;
  uint16_t len;
} XpStoredWord;
void xp_word_store_init(XpWordStore* state, const XpParaAllocator* allocator);
void xp_word_store_destroy(XpWordStore* state);
// Input is borrowed for the call (NULL only for len=0, len <= PTRDIFF_MAX).
// out is aligned, writable and disjoint from input/state. Failure leaves it unchanged.
// Copies at most 65534 bytes and appends NUL. 1=success, 0=OOM/invalid length.
uint8_t xp_word_store_append(XpWordStore* state, const uint8_t* text, size_t len, XpStoredWord* out);
// A handle/suffix is borrowed until release or destruction. Release exactly one
// of the original handle and its suffix aliases, never both. Handles belong to state.
const char* xp_word_store_cstr(const XpWordStore* state, XpStoredWord word);
void xp_word_store_release(XpWordStore* state, XpStoredWord word);
size_t xp_word_store_chunk_count(const XpWordStore* state);
const char* xp_word_store_chunk_data(const XpWordStore* state, size_t index);
size_t xp_word_store_chunk_used(const XpWordStore* state, size_t index);

// Token metadata uses caller-owned opaque state: 26 pointer-sized words,
// pointer-aligned. Same allocator, lifetime and serialization contract as above.
// Token flags: bit 0 continues, bit 1 no-space-before; remaining bits ignored.
// insert/reserve return 0 on failure without changing the token sequence; capacity
// may grow. get returns a zero token outside the sequence. Tokens borrow word handles;
// callers release word-text obligations before erasing metadata or destroying its arena.
typedef struct XpTokens XpTokens;
typedef struct {
  XpStoredWord word;
  uint8_t style, focus, link, slot, flags;
} XpToken;
void xp_tokens_init(XpTokens* state, const XpParaAllocator* allocator);
void xp_tokens_destroy(XpTokens* state);
size_t xp_tokens_size(const XpTokens* state);
void xp_tokens_get(const XpTokens* state, size_t index, XpToken* out);
void xp_tokens_set(XpTokens* state, size_t index, const XpToken* token);
uint8_t xp_tokens_reserve(XpTokens* state, size_t required);
uint8_t xp_tokens_insert(XpTokens* state, size_t index, const XpToken* token, uint32_t offset);
void xp_tokens_erase_prefix(XpTokens* state, size_t count);
uint32_t xp_tokens_offset(const XpTokens* state, size_t index);
uint8_t xp_tokens_record_absorbed(XpTokens* state, uint32_t offset);
// Updates the NFC cursor; requires exclusive access despite being a logical lookup.
uint32_t xp_tokens_source_after(XpTokens* state, uint32_t start, uint32_t rendered);

// Width-cache owner: initialize all fields to zero except allocator. Only Rust
// mutates ownership fields; C++ may read/write data[0..len] between calls. Movable
// by transferring the record and zeroing the source data/len/capacity. No copies.
// Growth starts at requested count, then doubles; failed is latched on OOM.
typedef struct {
  uint16_t* data;
  size_t len, capacity;
  const XpParaAllocator* allocator;
  uint8_t failed;
} XpLayoutWidths;
uint8_t xp_layout_widths_reserve(XpLayoutWidths* widths, size_t needed);
uint8_t xp_layout_widths_insert(XpLayoutWidths* widths, size_t index, uint16_t value);
void xp_layout_widths_destroy(XpLayoutWidths* widths);

// Same ownership and failure contract as XpLayoutWidths; usize break indices.
typedef struct {
  size_t* data;
  size_t len, capacity;
  const XpParaAllocator* allocator;
  uint8_t failed;
} XpLayoutBreaks;
uint8_t xp_layout_breaks_push(XpLayoutBreaks* breaks, size_t value);
void xp_layout_breaks_destroy(XpLayoutBreaks* breaks);

// Paragraph breaking callbacks are synchronous and may not reenter layout. The
// context and callback record outlive the call; no pointer is retained by Rust.
// widths describes the token sequence. Successful split inserts exactly one word
// and updates widths; other metric callbacks do not mutate the sequence.
// ruby intervals use exclusive ends. emit copies one break, returning 0 on OOM.
// Both entry points return 1 on success, 0 on allocation/size/output failure.
// Optimal allocates int32_t[N] + size_t[N] with the paragraph allocator; greedy
// has no Rust allocations. Caller owns widths, split mutation and output storage.
typedef struct {
  void* context;
  const XpLayoutWidths* widths;
  uint8_t has_ruby;
  uint8_t (*allows_break)(void*, size_t);
  int32_t (*gap)(void*, size_t);
  int32_t (*ruby_start)(void*, size_t, size_t);
  int32_t (*ruby_end)(void*, size_t, size_t);
  uint8_t (*split)(void*, size_t, int32_t, uint8_t);
  uint8_t (*emit)(void*, size_t);
} XpLayoutCallbacks;
uint8_t xp_layout_optimal(const XpLayoutCallbacks* callbacks, const XpParaAllocator* allocator, int32_t page,
                          int32_t indent);
uint8_t xp_layout_greedy(const XpLayoutCallbacks* callbacks, int32_t page, int32_t indent, size_t max_lines,
                         uint8_t first_consumed, uint8_t hyphenation);

// Split borrows disjoint stores exclusively. Callbacks cannot access those stores;
// measure borrows text for the call, inserted updates only ruby metadata.
// Result is written only on success; failed width growth latches widths.failed.
typedef struct {
  void* context;
  uint16_t (*measure)(void*, const uint8_t*, size_t, uint8_t, uint8_t, uint8_t);
  uint8_t (*boundary)(uint32_t, uint32_t);
  void (*inserted)(void*, size_t);
  size_t index;
  int32_t available, language;
  uint8_t style, fallback;
} XpLayoutSplitRequest;
typedef struct {
  size_t offset;
  int32_t width;
  uint32_t remainder_offset;
  uint16_t remainder_width;
  uint8_t hyphen;
} XpLayoutSplitResult;
uint8_t xp_layout_split(XpWordStore* store, XpTokens* tokens, XpLayoutWidths* widths,
                        const XpLayoutSplitRequest* request, XpLayoutSplitResult* result);

// Stable borrowed line in logical or MiniBidi visual order. gap queries the
// natural advance BEFORE index, with index > 0. Callbacks never reenter layout.
typedef struct {
  uint16_t width;
  uint8_t continues, space;
} XpLineWordMetrics;
typedef struct {
  void* context;
  XpLineWordMetrics (*word)(void*, size_t);
  int32_t (*gap)(void*, size_t);
} XpLineCallbacks;
typedef struct {
  size_t count;
  int32_t page, indent, extra_start, extra_end;
  uint8_t alignment, rtl, reordered, last;
} XpLineSpec;
typedef struct {
  int32_t width, natural_gaps;
  size_t gap_count;
  int32_t spare, justify;
} XpLineMetrics;
void xp_layout_line_metrics(const XpLineSpec* spec, const XpLineCallbacks* callbacks, XpLineMetrics* out);
// out borrows count writable i16 positions, disjoint from inputs. metrics belongs
// to the same unchanged spec/callback line. Casts preserve firmware i16 wrapping.
void xp_layout_line_position(const XpLineSpec* spec, const XpLineCallbacks* callbacks, const XpLineMetrics* metrics,
                             int16_t* out);

// Completed-line owner; initialize zero except allocator. One exact allocation:
// N pairs of u32 source offsets, N i16 x positions, N u8 styles, then optional
// N u8 focus boundaries and N u16 suffix advances. N <= 10000. No TextBlock wire
// format is shared here. Arrays are borrowed until destroy, without concurrent use.
typedef struct {
  uint8_t* data;
  size_t count;
  const XpParaAllocator* allocator;
  uint8_t focus;
} XpLayoutLine;
uint8_t xp_layout_line_init(XpLayoutLine* line, size_t count, uint8_t focus);
void xp_layout_line_destroy(XpLayoutLine* line);
uint8_t xp_layout_line_sources(XpLayoutLine* line, const XpWordStore* store, XpTokens* tokens, size_t start,
                               const uint16_t* order, uint8_t paragraph_emitted, uint16_t* paragraph_start);
size_t xp_layout_strip_soft_hyphens(uint8_t* data, size_t len);

// XML streaming parser. Handles and spans have a single externally serialized owner.
typedef struct XpXml XpXml;
typedef struct {
  const char* data;
  size_t len;
} XpXmlSpan;
typedef struct {
  uint32_t kind;
  uint32_t error;
  uint64_t byte;
  uint64_t line;
  XpXmlSpan name;
  XpXmlSpan value;
  const char* const* attributes;
  size_t attribute_count;
} XpXmlEvent;

enum XpXmlKind {
  XP_XML_START = 1,
  XP_XML_END,
  XP_XML_TEXT,
  XP_XML_COMMENT,
  XP_XML_PI,
  XP_XML_CDATA_START,
  XP_XML_CDATA_END,
  XP_XML_ENTITY,
  XP_XML_ERROR,
  XP_XML_NEED_INPUT,
  XP_XML_DONE,
  XP_XML_STOPPED
};
// Status: 0 success, -1 invalid argument, -2 allocation failure. Parse errors
// arrive as XP_XML_ERROR with Expat's numeric error code and source location.
int32_t xp_xml_create(XpXml** out_handle);
// Feed may consume a prefix. Drain next() to NEED_INPUT before feeding again;
// final applies only when the whole offered slice has been consumed.
int32_t xp_xml_feed(XpXml*, const uint8_t*, size_t, uint8_t final_input, size_t* consumed);
// Spans expire on the next call on this handle. START/END names and alternating
// START attribute name/value strings are NUL-terminated; attributes ends in NULL.
// TEXT/COMMENT/ENTITY use name as a length-delimited value; PI uses name + value.
int32_t xp_xml_next(XpXml*, XpXmlEvent*);
void xp_xml_stop(XpXml*);
void xp_xml_destroy(XpXml*);

// Shared text helpers. Buffers are borrowed for one call, never retained.
// Lengths must fit ptrdiff_t; nonempty buffers must be valid. Outputs must be
// writable and disjoint from inputs, except the explicitly in-place operation.
uint8_t xp_text_no_break_before(uint32_t cp);
uint8_t xp_text_no_break_after(uint32_t cp);
uint8_t xp_text_cjk(uint32_t cp);
uint8_t xp_text_hangul(uint32_t cp);
uint8_t xp_text_combining(uint32_t cp);
uint8_t xp_text_break_after_hyphen(uint32_t cp);
uint8_t xp_text_explicit_hyphen(uint32_t cp);
uint32_t xp_text_upper(uint32_t cp);
uint32_t xp_text_decomposed_base(uint32_t cp);
uint8_t xp_text_boundary(uint32_t left, uint32_t right);
uint8_t xp_text_opportunity(uint32_t left, uint32_t right);
uint32_t xp_utf8_next(const uint8_t* data, size_t len, size_t* consumed);
// out has four writable bytes. Encoder intentionally accepts non-scalar values.
size_t xp_utf8_encode(uint32_t cp, uint8_t* out);
size_t xp_utf8_safe_truncate(const uint8_t* data, size_t len);
size_t xp_utf8_remove_last(const uint8_t* data, size_t len);
uint8_t xp_utf8_may_compose(const uint8_t* data, size_t len);
// Callbacks may not mutate input, retain emitted bytes, or unwind. Returns zero
// when absorbed rejects an offset; the facade discards any emitted output.
uint8_t xp_utf8_compose(const uint8_t* data, size_t len, void (*emit)(void*, const uint8_t*, size_t),
                        uint8_t (*absorbed)(void*, uint32_t), void* context);
// len excludes the terminator; caller writes it at the returned length.
size_t xp_utf8_compose_in_place(uint8_t* data, size_t len);
// Returns required count, leaving out untouched if capacity is insufficient.
size_t xp_text_offsets(const uint8_t* data, size_t len, size_t* out, size_t capacity);

// One exclusive owner per handle. Inputs and writable outputs must be disjoint
// from the handle and each other; byte lengths are at most PTRDIFF_MAX.
// Records borrow storage until the next feed/next/destroy. Drain to NEED_INPUT
// before feeding; final_input takes effect after the whole supplied slice.
// Return codes: 0 success, -1 invalid argument, -2 create allocation failure.
// Live-parser failures are ERROR records (error=1 for OOM). Null destroy is safe.
typedef struct XpPkg XpPkg;
typedef struct {
  const uint8_t* data;
  size_t len;
} XpPkgSpan;
typedef struct {
  uint32_t kind;
  uint32_t error;
  XpPkgSpan value;
  XpPkgSpan href;
  XpPkgSpan anchor;
  uint8_t level;
  uint32_t field;
  XpPkgSpan properties;
  float number;
  uint8_t number_present;
} XpPkgRecord;
// CONTAINER_PATH uses value; TOC uses value/href/anchor/level; PROTECTION latches true.
// OPF uses field: metadata values are complete replacements; SERIES also supplies
// number/number_present. MANIFEST uses value=id, href, anchor=media type, properties;
// SPINE uses value=idref for lookup in the caller-owned SD index. CSS uses href.
// *_BEGIN/STORE_END control the caller's store; COMPLETE marks metadata completion.
enum {
  XP_PKG_NEED_INPUT = 0,
  XP_PKG_DONE = 1,
  XP_PKG_ERROR = 2,
  XP_PKG_CONTAINER_PATH = 3,
  XP_PKG_TOC = 4,
  XP_PKG_PROTECTION = 5,
  XP_PKG_OPF = 6
};
enum {
  XP_PKG_TITLE = 0,
  XP_PKG_AUTHOR = 1,
  XP_PKG_LANGUAGE = 2,
  XP_PKG_ISBN = 3,
  XP_PKG_ASIN = 4,
  XP_PKG_SERIES = 5,
  XP_PKG_NCX = 6,
  XP_PKG_NAV = 7,
  XP_PKG_COVER = 8,
  XP_PKG_GUIDE = 9,
  XP_PKG_TEXT = 10,
  XP_PKG_MANIFEST = 20,
  XP_PKG_SPINE = 21,
  XP_PKG_MANIFEST_BEGIN = 22,
  XP_PKG_SPINE_BEGIN = 23,
  XP_PKG_GUIDE_BEGIN = 24,
  XP_PKG_STORE_END = 25,
  XP_PKG_CSS = 26,
  XP_PKG_COMPLETE = 27
};
int32_t xp_pkg_opf_create(const uint8_t* base, size_t len, uint8_t metadata_only, uint8_t has_cache, XpPkg** out);
int32_t xp_pkg_container_create(const uint8_t* base, size_t len, XpPkg** out);
int32_t xp_pkg_ncx_create(const uint8_t* base, size_t len, XpPkg** out);
int32_t xp_pkg_nav_create(const uint8_t* base, size_t len, XpPkg** out);
int32_t xp_pkg_encryption_create(const uint8_t* base, size_t len, XpPkg** out);
int32_t xp_pkg_feed(XpPkg* handle, const uint8_t* bytes, size_t len, uint8_t final_input, size_t* consumed);
int32_t xp_pkg_next(XpPkg* handle, XpPkgRecord* out);
void xp_pkg_destroy(XpPkg* handle);

#ifdef __cplusplus
}
#endif
