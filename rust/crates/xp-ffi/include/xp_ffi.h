#pragma once

#include <stdbool.h>
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

// Paragraph owner in 64 pointer-sized, pointer-aligned words. Initialize/destroy once.
// Callback spans are borrowed for one call. Metrics cannot reenter the paragraph; the
// line sink may query scalars, but cannot mutate/destroy it or retain its spans.
typedef struct XpParagraph XpParagraph;
typedef struct XpChapterState XpChapterState;
typedef struct {
  const uint8_t* data;
  uint32_t len;
} XpParaSpan;
typedef struct {
  uint8_t alignment;
  int8_t character_spacing;
  int16_t margin_top;
  int16_t margin_bottom;
  int16_t margin_left;
  int16_t margin_right;
  int16_t padding_top;
  int16_t padding_bottom;
  int16_t padding_left;
  int16_t padding_right;
  int16_t text_indent;
  uint8_t text_indent_defined;
  uint8_t text_align_defined;
  uint8_t rtl;
  uint8_t direction_defined;
  float font_scale;
  uint8_t font_scale_defined;
  int32_t font_id;
  uint8_t break_before;
  uint8_t break_after;
  uint8_t keep_with_next;
} XpParaBlock;
typedef struct {
  void* context;
  void (*trace)(void*, XpParaSpan);
  // Nonempty input has a readable NUL at data[len]; len excludes it.
  // Other span callbacks are bounded by len and do not promise a trailing NUL.
  int32_t (*advance)(void*, int32_t, XpParaSpan, uint8_t, int8_t);
  int32_t (*space)(void*, int32_t, uint8_t);
  int32_t (*kerning)(void*, int32_t, uint32_t, uint32_t, uint8_t, int8_t);
  int32_t (*space_advance)(void*, int32_t, uint32_t, uint32_t, uint8_t);
  int32_t (*ascender)(void*, int32_t);
  uint8_t (*is_sd)(void*, int32_t);
  void (*prepare)(void*, int32_t, const XpParaSpan*, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t);
  uint8_t (*starts_rtl)(XpParaSpan, int32_t);
  uint8_t (*visual_order)(XpParaSpan, const uint16_t*, uint32_t, uint8_t, uint16_t*);
} XpParaMetrics;
typedef struct {
  char href[256];
  int16_t x, width, top_lift;
} XpParaLink;
typedef struct {
  XpParaSpan text;
  const uint16_t* offsets;
  uint32_t count;
  const int16_t* positions;
  const uint8_t* styles;
  const uint8_t* boundaries;
  const uint16_t* suffixes;
  const uint32_t* sources;
  const XpParaSpan* ruby;
  const XpParaLink* links;
  uint32_t link_count;
  const XpParaBlock* block;
  uint16_t paragraph_start;
  uint32_t visible_offset;
  const uint8_t* font_slots;
  const int32_t* fonts;
} XpParaLine;
typedef struct {
  int32_t font;
  uint16_t width;
  uint8_t include_last;
  int8_t tracking;
  uint8_t spacing;
  uint32_t max_lines;
  int32_t language;
  const uint8_t* stop;
  void* context;
  uint8_t (*emit)(void*, const XpParaLine*);
} XpParaLayout;
void xp_para_init(XpParagraph*, const XpParaAllocator*, const XpParaBlock*, uint8_t hyphenation, uint8_t focus,
                  uint8_t indent, uint8_t (*starts_rtl)(XpParaSpan, int32_t));
void xp_para_destroy(XpParagraph*);
// Retains a borrowed paragraph (or null). Rebind before destroying/replacing it;
// chapter calls and paragraph mutation must be serialized with assembly.
void xp_para_bind_chapter(XpChapterState*, XpParagraph*);
void xp_para_add_word(XpParagraph*, XpParaSpan, uint8_t style, uint8_t underline, uint8_t attach, uint32_t offset,
                      uint8_t link, uint8_t slot, uint8_t no_space);
uint8_t xp_para_add_link(XpParagraph*, XpParaSpan);
uint8_t xp_para_link_matches(const XpParagraph*, uint8_t, XpParaSpan);
// Result borrows paragraph storage until its next mutation or destruction.
XpParaSpan xp_para_ruby(const XpParagraph*, uint32_t);
void xp_para_set_ruby(XpParagraph*, uint32_t, XpParaSpan);
void xp_para_set_ruby_group(XpParagraph*, uint32_t, uint32_t, XpParaSpan);
// Result borrows paragraph storage until its next mutation or destruction.
XpParaSpan xp_para_word(const XpParagraph*, uint32_t);
uint8_t xp_para_indent(const XpParagraph*);
uint32_t xp_para_size(const XpParagraph*);
uint8_t xp_para_dropped(const XpParagraph*);
int32_t xp_para_lines_after(const XpParagraph*);
uint8_t xp_para_word_style(const XpParagraph*, uint32_t);
void xp_para_token(const XpParagraph*, uint32_t, XpToken*);
void xp_para_get_style(const XpParagraph*, XpParaBlock*);
void xp_para_set_style(XpParagraph*, const XpParaBlock*);
void xp_para_suppress_indent(XpParagraph*);
void xp_para_reset_indent(XpParagraph*);
uint8_t xp_para_size_slot(XpParagraph*, float);
uint8_t xp_para_slot_count(const XpParagraph*);
float xp_para_slot_scale(const XpParagraph*, uint8_t);
void xp_para_slot_font(XpParagraph*, uint8_t, int32_t);
void xp_para_layout(XpParagraph*, const XpParaMetrics*, const XpParaLayout*);

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
  float number;
  uint8_t number_present;
} XpPkgRecord;
// CONTAINER_PATH uses value; TOC uses value/href/anchor/level; PROTECTION latches true.
// OPF uses field: metadata values are complete replacements; SERIES also supplies
// number/number_present. MANIFEST uses value=id and href;
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

// One exclusive owner per pass/cache; destroy passes before their borrowed cache.
// Calls and callbacks are synchronous: no concurrent access, callback reentry into
// either active handle (including queries/destruction), or unwinding across C.
// Input spans are readable until return, disjoint from mutable state, and at most
// PTRDIFF_MAX bytes; null data is valid only at length zero. Rust never frees them.
typedef struct XpPkgPass XpPkgPass;
typedef struct XpPkgCache XpPkgCache;
enum { XP_PKG_CONTAINER = 1, XP_PKG_NCX_PASS, XP_PKG_NAV_PASS, XP_PKG_ENCRYPTION, XP_PKG_OPF_PASS };
enum { XP_PKG_ITEMS, XP_PKG_SPINE_FILE, XP_PKG_TOC_FILE, XP_PKG_BOOK_FILE };
enum {
  XP_PKG_OPEN_READ,
  XP_PKG_OPEN_WRITE,
  XP_PKG_CLOSE,
  XP_PKG_REMOVE,
  XP_PKG_IS_OPEN,
  XP_PKG_POSITION,
  XP_PKG_SIZE,
  XP_PKG_BEGIN_PASS,
  XP_PKG_END_PASS,
  XP_PKG_BEGIN_BUILD,
  XP_PKG_END_BUILD,
  XP_PKG_BEGIN_SIZES,
  XP_PKG_END_SIZES
};
// Non-null tables require control/read/write/seek; item_size/observe are optional.
// Pass creation copies tables, but borrows their contexts through pass destruction.
// Direct cache calls borrow the table/context only until return. A zeroed table is
// invalid; use a null table pointer to omit an optional pass sink.
// control uses ITEMS for the manifest store; SPINE_FILE/TOC_FILE for pass stores;
// BOOK_FILE for the final output. OPEN_READ/OPEN_WRITE/CLOSE/REMOVE/IS_OPEN return
// boolean success/state; POSITION/SIZE return byte offsets/lengths. OPEN_WRITE
// truncates. BEGIN_PASS/END_PASS select/flush a spine or TOC buffered writer;
// BEGIN_BUILD/END_BUILD on BOOK_FILE select/flush all three build streams;
// BEGIN_SIZES/END_SIZES on SPINE_FILE acquire/release the ZIP size service.
// BEGIN_PASS/BEGIN_BUILD results are ignored; END_PASS/END_BUILD and BEGIN_SIZES
// must report success. Cleanup CLOSE/REMOVE/END_SIZES results are ignored.
// read/write return byte counts, seek returns boolean success. A buffered write
// may accept all bytes and latch failure, but the matching END must report it.
// item_size returns uncompressed bytes for a zero-based spine index (absent => 0).
// observe is host-trace-only: kind 0 spine, 1 TOC, before validation/normalization.
// Callback spans/buffers expire on return and must not be retained or freed.
typedef struct {
  void* context;
  uint32_t (*control)(void*, uint32_t file, uint32_t operation);
  size_t (*read)(void*, uint32_t file, uint8_t*, size_t);
  size_t (*write)(void*, uint32_t file, const uint8_t*, size_t);
  uint8_t (*seek)(void*, uint32_t file, uint32_t offset);
  uint32_t (*item_size)(void*, uint16_t index, XpPkgSpan href);
  void (*observe)(void*, uint32_t kind, XpPkgSpan title, XpPkgSpan href, XpPkgSpan anchor, uint8_t level);
} XpPkgIo;
// Creates one document pass: size is the exact XML byte length (unused for
// encryption); base is copied. items may be null when no manifest store is needed;
// a non-null cache requires cache_io. Create returns 0, -1 invalid, -2 OOM.
// metadata_only is 0 or 1 (OPF only); an invalid value returns -1 and leaves out
// unchanged. Every other failure clears a valid out pointer.
int32_t xp_pkg_pass_create(uint32_t kind, XpPkgSpan base, size_t size, uint8_t metadata_only, const XpPkgIo* items,
                           XpPkgCache* cache, const XpPkgIo* cache_io, XpPkgPass** out);
// Null-safe; OPF destruction also closes/removes ITEMS via the borrowed context.
void xp_pkg_pass_destroy(XpPkgPass*);
// Print-compatible accepted byte count; metadata-only OPF deliberately short-writes
// when complete. Sized passes finalize on their last bytes. Encryption requires
// finish; finish returns 0 on failure/already complete/OPF, otherwise 1.
size_t xp_pkg_pass_write(XpPkgPass*, XpPkgSpan);
uint8_t xp_pkg_pass_finish(XpPkgPass*);
// OPF fields use XP_PKG_TITLE..XP_PKG_TEXT; CSS paths start at field 32.
// Simple passes expose their path regardless of field. Unknown/absent values are
// empty. Returned spans expire on write/finish/destroy; they are not NUL-terminated.
XpPkgSpan xp_pkg_pass_value(const XpPkgPass*, uint32_t field);
// Returns 1 and writes the series index when present, otherwise 0 without writing.
uint8_t xp_pkg_pass_index(const XpPkgPass*, float*);
// OPF: CSS count. Simple passes: encryption protection latch (0 or 1).
size_t xp_pkg_pass_count(const XpPkgPass*);
// Create returns 0, -1 invalid out, -2 OOM; clears a valid out on failure.
int32_t xp_pkg_cache_create(XpPkgCache**);
// Null-safe; releases Rust state, not caller-owned file handles.
void xp_pkg_cache_destroy(XpPkgCache*);
enum { XP_PKG_BEGIN_OPF, XP_PKG_END_OPF, XP_PKG_BEGIN_TOC, XP_PKG_END_TOC, XP_PKG_END_WRITE };
// Caller brackets OPF and TOC passes with their matching BEGIN/END, then END_WRITE
// and build. Destroy each document pass while its contexts/cache are still alive.
// Cache control/entry/build return 0 on success, negative on argument/OOM/I/O/state
// failure. The sink owner remains responsible for file cleanup after early errors.
int32_t xp_pkg_cache_control(XpPkgCache*, const XpPkgIo*, uint32_t operation);
// kind 0 writes a spine href; kind 1 writes title/href/anchor/level for a TOC entry.
int32_t xp_pkg_cache_entry(XpPkgCache*, const XpPkgIo*, uint32_t kind, XpPkgSpan title, XpPkgSpan href,
                           XpPkgSpan anchor, uint8_t level);
// Metadata order: title, author, language, cover href, text-reference href.
// build flushes/closes all opened build streams and removes incomplete book output
// after record/flush failure; temporary stores remain caller-owned.
int32_t xp_pkg_cache_build(XpPkgCache*, const XpPkgIo*, const XpPkgSpan metadata[5]);
// toc == 0 selects spine count; any nonzero value selects TOC count.
uint16_t xp_pkg_cache_count(const XpPkgCache*, uint8_t toc);

// Section cache codec. All storage is caller-owned; no allocation or callbacks.
// Buffers must have len/capacity bytes in one allocation (<= PTRDIFF_MAX),
// remain alive while borrowed by a decoder, and not overlap output records.
// NULL is permitted only for zero-length buffers. Status: 0 OK, 1 NEED_INPUT,
// 2 NEED_OUTPUT, 3 INVALID. Failed encode leaves output untouched.
enum { XP_SEC_OK = 0, XP_SEC_NEED_INPUT = 1, XP_SEC_NEED_OUTPUT = 2, XP_SEC_INVALID = 3 };
enum { XP_SEC_VERSION = 68, XP_SEC_PARTIAL_VERSION = 214, XP_SEC_HEADER_SIZE = 44 };
typedef enum {
  XP_SEC_U8,
  XP_SEC_U16,
  XP_SEC_U32,
  XP_SEC_HEADER,
  XP_SEC_TEXT_HEADER,
  XP_SEC_BLOCK_STYLE,
  XP_SEC_POSITION,
  XP_SEC_IMAGE_SIZE,
  XP_SEC_RULE,
  XP_SEC_DROP_CAP,
  XP_SEC_BORDER,
  XP_SEC_ELEMENT_TAG,
  XP_SEC_FOOTNOTE_COUNT,
  XP_SEC_LINK_COUNT,
  XP_SEC_LINK_GEOMETRY,
  XP_SEC_WORD_FONT_FLAG
} XpSecKind;
typedef struct {
  uint32_t kind;
  uint32_t values[20];
} XpSecRecord;
typedef struct {
  const uint8_t* input;
  size_t len;
  size_t position;
  uint32_t kind;
} XpSecDecoder;
size_t xp_sec_record_size(uint32_t kind);
uint32_t xp_sec_encode_record(const XpSecRecord* record, uint8_t* output, size_t capacity, size_t* required);
// A decoder walks consecutive records of its declared kind; initialize position
// to zero. Payload owners use separate arena/font validators after direct reads.
uint32_t xp_sec_decode_next(XpSecDecoder* decoder, XpSecRecord* record);
size_t xp_sec_arena_size(uint16_t words, uint8_t focus, uint16_t text_bytes);
uint8_t xp_sec_validate_arena(uint16_t words, uint8_t focus, uint16_t text_bytes, const uint8_t* data, size_t len);
uint8_t xp_sec_validate_fonts(uint16_t words, const uint8_t* data, size_t len);
uint8_t xp_sec_header_matches(const XpSecRecord* header, const XpSecRecord* expected);
uint8_t xp_sec_table_entry(uint32_t offset, uint32_t index, uint32_t width, uint32_t size, uint32_t minimum,
                           uint32_t* result);
uint8_t xp_sec_partial_trailer(const XpSecRecord* header, uint32_t size, uint32_t* result);

uint32_t xp_sec_encode_text_word(size_t previous, const uint8_t* word, size_t len, uint8_t* output, size_t capacity,
                                 size_t* required);

/* Chapter semantics: synchronous borrowed spans, one fallible owning handle.
 * CSS and sink/metrics/context outlive the handle. Only measure_table may use
 * its supplied state reborrow for table feedback; no other callback may access
 * chapter state, feed, finish or destroy the handle. No state/span pointer may
 * be retained after its synchronous operation. */
typedef struct XpChapter XpChapter;
typedef struct XpChapterState XpChapterState;
typedef struct {
  const uint8_t* data;
  uint32_t len;
} XpChapterSpan;
typedef struct {
  int32_t font;
  float compression;
  uint16_t width, height;
  bool extra_spacing;
  uint8_t alignment, indent_spaces;
  int8_t character_spacing;
  uint8_t word_spacing;
  bool hyphenation, focus;
} XpChapterAssemblyOptions;
typedef struct {
  uint8_t alignment, embedded, images, css_enabled;
  XpChapterAssemblyOptions assembly;
} XpChapterOptions;
typedef struct {
  uint8_t alignment;
  int8_t character_spacing;
  int16_t margin_top;
  int16_t margin_bottom;
  int16_t margin_left;
  int16_t margin_right;
  int16_t padding_top;
  int16_t padding_bottom;
  int16_t padding_left;
  int16_t padding_right;
  int16_t text_indent;
  uint8_t text_indent_defined;
  uint8_t text_align_defined;
  uint8_t rtl;
  uint8_t direction_defined;
  float font_scale;
  uint8_t font_scale_defined;
  int32_t font_id;
  uint8_t break_before;
  uint8_t break_after;
  uint8_t keep_with_next;
} XpChapterBlock;
typedef struct {
  uint8_t text[13], length, codepoints, lines, bold, first_pending;
  int32_t span_depth;
} XpChapterDropCap;
typedef struct {
  uint8_t number[32], href[256];
} XpChapterFootnote;
typedef struct {
  uint16_t min[4], preferred[4];
  uint8_t columns;
} XpChapterMeasure;
typedef struct {
  uint32_t kind, words[10];
  XpChapterSpan first, second;
  const XpChapterBlock* block;
} XpChapterTrace;
typedef struct {
  void* pointer;
} XpChapterPayload;
typedef struct {
  uint16_t width;
  uint8_t thickness;
} XpChapterRule;
typedef struct {
  int32_t font;
  uint16_t scale256;
  uint8_t style;
  uint8_t text[13];
} XpChapterCap;
typedef struct {
  uint8_t width, style;
} XpChapterBorder;
typedef struct {
  uint16_t width, height;
  XpChapterBorder sides[4];
  bool shaded;
} XpChapterBox;
typedef struct {
  uint8_t kind; /* line=0, image=1, rule=2, drop cap=3, border box=4 */
  union {
    XpChapterPayload payload;
    XpChapterRule rule;
    XpChapterCap cap;
    XpChapterBox box;
  } data;
} XpChapterContent;
typedef struct {
  int16_t x, y;
  XpChapterContent content;
} XpChapterElement;
typedef struct {
  uint8_t href[256];
  int16_t x, y, width, height;
} XpChapterLink;
typedef struct {
  uint8_t href[256];
  int16_t x, width, top_lift;
} XpChapterLineLink;
typedef struct {
  int16_t x;
  uint32_t words;
  int32_t base_height, ruby_shift;
} XpChapterLineMetrics;
typedef struct {
  uint16_t paragraph, list;
  uint32_t visible;
  int16_t next_y;
} XpChapterPosition;
/* Borrowed sink/metrics/context outlive the chapter. create_page always consumes
 * every line/image payload, including failure (null). page_links/page_footnotes
 * borrow their arrays until return; Rust then frees each source buffer. complete
 * consumes the C++ page after these copies. Input spans are borrowed
 * until return; returned line_links views last until release_line_links,
 * pending_anchor until defer/record. Only measure_table may use its supplied state. */
typedef struct {
  const XpParaAllocator* (*allocator)(void);
  const XpParaMetrics* metrics;
  int32_t (*language)(void);
  void (*reclaim_paragraph_memory)(void);
  XpChapterSpan (*entity_lookup)(void* context, XpChapterSpan name);
  uint8_t* (*entity_buffer)(void* context, uint32_t length);
  int32_t (*font_variant)(void* context, int32_t font, float scale);
  int32_t (*line_height)(void* context, int32_t font, float compression);
  uint8_t (*codepoint)(void* context, int32_t font, uint32_t codepoint, uint8_t style, int32_t* advance, int32_t* top);
  /* Null create results report allocation failure. create_line borrows XpParaLine
   * only until return; a non-null payload belongs to Rust until consumed/dropped. */
  void* (*create_line)(void* context, const XpParaLine* line);
  XpChapterLineMetrics (*line_metrics)(void* context, const void* line, const XpChapterAssemblyOptions* options,
                                       bool table_row);
  void (*set_margin)(void* line, int16_t left);
  const XpChapterLineLink* (*line_links)(void* context, void* line, uint32_t* count);
  /* Called once after each line_links borrow, including when copying fails. */
  void (*release_line_links)(void* context);
  /* create_page consumes all payloads even on null. A non-null C++ page is passed
   * through the two copy callbacks and consumed exactly once by complete. */
  void* (*create_page)(void* context, const XpChapterElement* elements, uint32_t count);
  void (*page_links)(void* page, const XpChapterLink* links, uint32_t count);
  void (*page_footnotes)(void* page, const XpChapterFootnote* footnotes, uint32_t count);
  /* 0 = success, 1 = stop, other = allocation failure. A page callback may set
   * a separate C++ stop latch; complete returns it after completePageFn returns. */
  uint8_t (*complete)(void* context, void* page, XpChapterPosition position);
  /* 0 = unsupported, 1 = failed, other = ready. Ready may be followed by one
   * create_image after an intervening text flush. The context owns the probed
   * paths until the next probe; Rust owns no temporary image allocation. */
  uint8_t (*probe_image)(void* context, XpChapterSpan source, int32_t* counter, bool* popup_shown, uint16_t* width,
                         uint16_t* height);
  void* (*create_image)(void* context, int32_t width, int32_t height);
  XpChapterSpan (*pending_anchor)(void* context);
  void (*defer_anchor)(void* context, XpChapterSpan id);
  uint8_t (*anchor_is_toc)(void* context, XpChapterSpan id);
  uint32_t (*anchor_count)(void* context);
  void (*record_anchor)(void* context, uint16_t page);
  void (*measure_table)(void* context, XpChapterState* state);
  void (*trace)(void* context, const XpChapterTrace* observation);
  void (*trace_bytes)(void* context, XpChapterSpan bytes);
} XpChapterSink;
/* Fixed C++ sink destructor; accepts one owned TextBlock or ImageBlock. */
void xp_chapter_release_block(void* block);
typedef enum {
  XP_CHAPTER_QUERY_STOPPED = 0,
  XP_CHAPTER_QUERY_OOM = 1,
  XP_CHAPTER_QUERY_ENDED = 2,
  XP_CHAPTER_QUERY_DEPTH = 3,
  XP_CHAPTER_QUERY_TABLE_DEPTH = 4,
  XP_CHAPTER_QUERY_VISIBLE_OFFSET = 5,
  XP_CHAPTER_QUERY_PARAGRAPH_INDEX = 6,
  XP_CHAPTER_QUERY_LIST_INDEX = 7,
  XP_CHAPTER_QUERY_ALIGNMENT = 8,
  XP_CHAPTER_QUERY_PRESERVE_WHITESPACE = 9,
  XP_CHAPTER_QUERY_MARKER_ONLY = 10,
  XP_CHAPTER_QUERY_HAS_WORD = 11,
  XP_CHAPTER_QUERY_BLOCK_DEPTH = 12,
  XP_CHAPTER_QUERY_INLINE_VALID = 13,
  XP_CHAPTER_QUERY_INLINE_SCALE = 14,
  XP_CHAPTER_QUERY_PAGE_BREAK = 15,
  XP_CHAPTER_QUERY_CELL_OPEN = 16,
  XP_CHAPTER_QUERY_COLUMN_COUNT = 17,
  XP_CHAPTER_QUERY_COLUMN0 = 18,
  XP_CHAPTER_QUERY_COLUMN1 = 19,
  XP_CHAPTER_QUERY_COLUMN2 = 20,
  XP_CHAPTER_QUERY_COLUMN3 = 21,
  XP_CHAPTER_QUERY_TABLE_BORDER = 22,
} XpChapterQuery;
typedef enum {
  XP_CHAPTER_NOTIFY_STOP = 0,
  XP_CHAPTER_NOTIFY_FAIL_LAYOUT = 1,
  XP_CHAPTER_NOTIFY_CONSUME_MARKER = 2,
  XP_CHAPTER_NOTIFY_CLEAR_CONTINUATION = 3,
  XP_CHAPTER_NOTIFY_RESET_INLINE_SIZE = 4,
  XP_CHAPTER_NOTIFY_FLUSH_WORD = 5,
  XP_CHAPTER_NOTIFY_CLEAR_DROP_CAP = 6,
  XP_CHAPTER_NOTIFY_DISARM_DROP_CAP = 7,
  XP_CHAPTER_NOTIFY_CONSUME_PAGE_BREAK = 8,
  XP_CHAPTER_NOTIFY_CLOSE_CELL = 9,
  XP_CHAPTER_NOTIFY_BEGIN_BLOCKS = 10,
  XP_CHAPTER_NOTIFY_BEGIN_LISTS = 11,
} XpChapterNotify;
int32_t xp_chapter_create(XpChapter** out, const XpChapterSink* sink, void* ctx, const XpCss* css,
                          const XpChapterOptions* options);
void xp_chapter_destroy(XpChapter* handle);
uint8_t xp_chapter_begin(XpChapter* handle);
uint8_t xp_chapter_finish(XpChapter* handle);
XpChapterState* xp_chapter_state(XpChapter* handle);
/* Feed at most 1024 bytes and drain internally: More=0, Done=1, Error=2. */
uint8_t xp_chapter_feed(XpChapter* handle, const uint8_t* data, uint32_t len, uint8_t final_input);
uint32_t xp_chapter_query(const XpChapterState* state, uint32_t query);
uint32_t xp_chapter_notify(XpChapterState* state, uint32_t notification);
void xp_chapter_block(const XpChapterState* state, XpChapterBlock* out);
void xp_chapter_drop_cap(const XpChapterState* state, XpChapterDropCap* out);
uint64_t xp_chapter_byte(const XpChapterState* state);
typedef uint32_t (*XpChapterRead)(void* ctx, uint8_t* bytes, uint32_t capacity);
uint8_t xp_chapter_measure_table(XpChapterState* state, void* ctx, XpChapterRead read, XpChapterMeasure* out);
void xp_chapter_plan_columns(XpChapterState* state, const XpChapterMeasure* measure);
uint32_t xp_chapter_handle_size(void);

// Image pipeline: caller-owned opaque storage; init/prepare/rows/finish/destroy
// are synchronous and exclusive. Never copy initialized storage. Destroy exactly
// once after successful init, before callback context or renderer expires.
// Prepare once before rows; begin-cache at most once after prepare. Finish at
// most once after all rows. Destroy also aborts an unfinished cache.
// Inputs and targets must not alias storage. Row spans borrow decoder memory for
// one call; target data is exclusively writable for len bytes until the next
// target callback or the enclosing ABI call returns. IO callbacks must preserve
// the active target loan (no drawing, buffer replacement, or renderer reentry).
// IO operations: 0 open, 1 write all bytes, 2 close, 3 close/remove partial file.
// Callbacks must not reenter. Kind: PNG=0, JPEG=1, GIF=2. Pixel types follow PNG.
typedef struct {
  uintptr_t words[48];
} XpImageStorage;
typedef struct {
  uintptr_t words[8];
} XpImageProbe;
// Placement and screen bounds use logical, orientation-adjusted pixel coordinates.
typedef struct {
  int32_t x, y, max_width, max_height, screen_width, screen_height;
  float crop_x, crop_y;
  uint8_t exact, dither, alpha, kind;
} XpImageConfig;
typedef struct {
  int32_t width, height, denominator, max_rows;
} XpImagePlan;
// Target geometry uses physical panel pixels: width/height are the full panel,
// origin is the first lent physical Y row, rows is the lent row count, and stride
// is bytes per physical row. Data starts at origin; len bounds the lent bytes.
// Mode: 0 BW, 1 grayscale LSB, 2 grayscale MSB; absolute selects absolute planes.
// Orientation: 0 portrait, 1 landscape clockwise, 2 portrait inverted,
// 3 landscape counterclockwise (native panel coordinates).
typedef struct {
  uint8_t* data;
  size_t len;
  int32_t stride, origin, rows, width, height;
  uint8_t mode, orientation, absolute;
} XpImageTarget;
typedef struct {
  void* context;
  void (*target)(void*, XpImageTarget*);
  bool (*io)(void*, uint32_t operation, const uint8_t*, size_t);
} XpImageCallbacks;
typedef struct {
  int32_t x, y, width, height, stride;
  const uint8_t* pixels;
  size_t len;
  // PNG: RGB888 plus optional alpha table. GIF: 256 preconverted gray bytes.
  const uint8_t* palette;
  size_t palette_len;
  uint32_t transparent;
  uint8_t pixel_type, depth, has_alpha;
} XpImageRow;
bool xp_image_init(void*, const XpImageConfig*, int32_t width, int32_t height, bool progressive,
                   const XpImageCallbacks*, XpImagePlan*);
// Convert 256 RGB888 entries to 256 gray entries; spans must be valid and disjoint.
void xp_image_gray_palette(const uint8_t* rgb, uint8_t* gray);
bool xp_image_prepare(void*);
bool xp_image_cache_begin(void*);
bool xp_image_row(void*, const XpImageRow*);
// Returns cache-written; success separately reports row/gap processing failure.
bool xp_image_finish(void*, bool* success);
void xp_image_destroy(void*);
void xp_image_packed_row(const XpImageTarget*, int32_t x, int32_t y, int32_t width, const uint8_t* row, size_t len);
void xp_image_probe_init(void*);
size_t xp_image_probe_write(void*, const uint8_t*, size_t);
bool xp_image_probe_dimensions(const void*, uint32_t* width, uint32_t* height);

/* MiniBidi compatibility. No allocation, retained pointers, or global state.
 * Workspace: allocate workspace_size() bytes aligned to workspace_align(),
 * initialize once, then reuse exclusively; no destroy is needed. Do not put
 * this workspace on the firmware task stack. All input/output/work spans must
 * be disjoint. Null span pointers are accepted only for zero length.
 * Text is length-bounded and stops at NUL or the first replacement character.
 * Status: 1 applied, 0 unchanged/fallback, -1 short output, -2 invalid argument.
 * apply_visual leaves output unchanged on failure and does not append NUL;
 * written is the byte count on success or required size on short output.
 * word_order requires capacity == count and NUL-terminated packed words.
 * Ignore word_order output unless status == 1 (C++ also writes partial results).
 * The original 128-codepoint/128-word limits still return 0, not an error.
 */
size_t xp_bidi_workspace_size(void);
size_t xp_bidi_workspace_align(void);
int32_t xp_bidi_workspace_init(void* work, size_t size);
uint8_t xp_bidi_class(uint32_t cp);
uint32_t xp_bidi_mirror(uint32_t cp);
uint8_t xp_bidi_transparent_mark(uint32_t cp);
uint8_t xp_bidi_starts_rtl(const uint8_t* data, size_t len, int32_t depth);
int32_t xp_bidi_paragraph_level(const uint8_t* data, size_t len, int32_t fallback, int32_t depth);
int32_t xp_bidi_apply_visual(const uint8_t* data, size_t len, int32_t paragraph, void* work, size_t work_size,
                             uint8_t* out, size_t capacity, size_t* written);
int32_t xp_bidi_word_order(const uint8_t* data, size_t len, const uint16_t* offsets, size_t count, uint8_t rtl,
                           void* work, size_t work_size, uint16_t* out, size_t capacity);

#ifdef __cplusplus
}
#endif
