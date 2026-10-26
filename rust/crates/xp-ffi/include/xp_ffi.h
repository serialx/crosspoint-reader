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

// Paragraph owner in 64 pointer-sized, pointer-aligned words. Initialize/destroy once.
// All spans are borrowed for one call. Metrics cannot reenter the paragraph; the
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
void xp_para_bind_chapter(XpChapterState*, XpParagraph*);
void xp_para_add_word(XpParagraph*, XpParaSpan, uint8_t style, uint8_t underline, uint8_t attach, uint32_t offset,
                      uint8_t link, uint8_t slot, uint8_t no_space);
uint8_t xp_para_add_link(XpParagraph*, XpParaSpan);
uint8_t xp_para_link_matches(const XpParagraph*, uint8_t, XpParaSpan);
XpParaSpan xp_para_ruby(const XpParagraph*, uint32_t);
void xp_para_set_ruby(XpParagraph*, uint32_t, XpParaSpan);
void xp_para_set_ruby_group(XpParagraph*, uint32_t, uint32_t, XpParaSpan);
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
 * CSS and the static services table outlive the handle. Services may reenter only
 * the feedback API using their supplied state pointer, never feed or destroy.
 * No state/span pointer may be retained after its synchronous operation. */
typedef struct XpChapter XpChapter;
typedef struct XpChapterState XpChapterState;
typedef struct {
  const uint8_t* data;
  uint32_t len;
} XpChapterSpan;
typedef struct {
  uint8_t alignment, embedded, images, css_enabled;
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
  /* Shared immutable entity result, or {NULL, 0} for an unknown spelling. */
  XpChapterSpan (*entity_lookup)(void*, XpChapterState*, XpChapterSpan);
  /* Writable driver scratch: XML feed already copied the input. */
  uint8_t* (*entity_buffer)(void*, XpChapterState*, uint32_t);
  void (*list_marker)(void* ctx, XpChapterState* state, XpChapterSpan text, uint32_t offset);
  void (*preserve_space)(void* ctx, XpChapterState* state);
  uint8_t (*paragraph_empty)(void* ctx, XpChapterState* state);
  uint32_t (*paragraph_size)(void* ctx, XpChapterState* state);
  float (*paragraph_scale)(void*, XpChapterState*);
  void (*paragraph_style)(void* ctx, XpChapterState* state, XpChapterBlock* out);
  void (*suppress_indent)(void* ctx, XpChapterState* state);
  void (*ruby_capacity)(void* ctx, XpChapterState* state);
  uint8_t (*add_link)(void* ctx, XpChapterState* state, XpChapterSpan href);
  uint8_t (*link_matches)(void* ctx, XpChapterState* state, uint8_t id, XpChapterSpan href);
  uint8_t (*size_slot)(void* ctx, XpChapterState* state, float scale);
  void (*set_paragraph_style)(void* ctx, XpChapterState* state, const XpChapterBlock* style);
  uint8_t (*word_style)(void* ctx, XpChapterState* state, uint32_t index);
  void (*ruby_group)(void* ctx, XpChapterState* state, uint32_t index, uint32_t count, XpChapterSpan text);
  void (*ruby_append)(void* ctx, XpChapterState* state, uint32_t index, XpChapterSpan text);
  void (*add_word)(void* ctx, XpChapterState* state, XpChapterSpan word, uint8_t style, uint8_t underline,
                   uint8_t attach, uint32_t offset, uint8_t link, uint8_t slot, uint8_t no_space);
  uint8_t (*paragraph_exists)(void* ctx, XpChapterState* state);
  void (*create_paragraph)(void* ctx, XpChapterState* state, const XpChapterBlock* style);
  void (*reset_paragraph)(void* ctx, XpChapterState* state);
  uint8_t (*body_sd)(void* ctx, XpChapterState* state);
  int32_t (*body_space)(void* ctx, XpChapterState* state);
  void (*prepare_text)(void* ctx, XpChapterState* state, XpChapterSpan text, uint8_t style);
  int32_t (*text_advance)(void* ctx, XpChapterState* state, XpChapterSpan text, uint8_t style);
  int32_t (*body_ascender)(void* ctx, XpChapterState* state);
  uint16_t (*page_width)(void* ctx, XpChapterState* state);
  uint16_t (*page_height)(void* ctx, XpChapterState* state);
  void (*clear_cells)(void* ctx, XpChapterState* state);
  void (*reserve_cells)(void* ctx, XpChapterState* state);
  uint32_t (*buffered_words)(void* ctx, XpChapterState* state);
  uint8_t (*table_stacked)(void* ctx, XpChapterState* state);
  void (*set_stacked)(void* ctx, XpChapterState* state, uint8_t value);
  void (*set_rtl)(void* ctx, XpChapterState* state, uint8_t value);
  void (*reset_table_bytes)(void* ctx, XpChapterState* state);
  uint32_t (*table_bytes)(void* ctx, XpChapterState* state);
  void (*add_table_bytes)(void* ctx, XpChapterState* state, uint32_t count);
  void (*reset_extracted)(void* ctx, XpChapterState* state);
  int32_t (*extracted)(void* ctx, XpChapterState* state);
  void (*footnote)(void* ctx, XpChapterState* state, int32_t index, const XpChapterFootnote* entry);
  uint8_t (*box_at_depth)(void* ctx, XpChapterState* state, int32_t depth);
  uint8_t (*image)(void* ctx, XpChapterState* state, XpChapterSpan src, const uint8_t* css);
  void (*measure_table)(void* ctx, XpChapterState* state);
  void (*start_block)(void* ctx, XpChapterState* state, const XpChapterBlock* style, uint8_t paragraph_end);
  void (*flush_anchor)(void* ctx, XpChapterState* state);
  void (*open_box)(void* ctx, XpChapterState* state, XpChapterBlock* style, const uint8_t* css);
  void (*close_box)(void* ctx, XpChapterState* state);
  void (*line_break)(void* ctx, XpChapterState* state, const XpChapterBlock* style);
  void (*table_fallback)(void* ctx, XpChapterState* state);
  void (*close_cell)(void* ctx, XpChapterState* state);
  void (*finish_row)(void* ctx, XpChapterState* state);
  void (*make_pages)(void* ctx, XpChapterState* state, uint8_t last_line, uint8_t paragraph_end);
  void (*horizontal_rule)(void* ctx, XpChapterState* state, const XpChapterBlock* style);
  void (*direction)(void* ctx, XpChapterState* state, uint8_t defined, uint8_t rtl);
  uint8_t (*anchor_is_toc)(void* ctx, XpChapterState* state, XpChapterSpan id);
  uint32_t (*anchor_count)(void* ctx, XpChapterState* state);
  uint8_t (*has_anchor)(void* ctx, XpChapterState* state);
  void (*defer_anchor)(void* ctx, XpChapterState* state, XpChapterSpan id);
  void (*trace)(void* ctx, XpChapterState* state, const XpChapterTrace* record);
} XpChapterServices;
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
int32_t xp_chapter_create(XpChapter** out, const XpChapterServices* services, void* ctx, const XpCss* css,
                          const XpChapterOptions* options);
void xp_chapter_destroy(XpChapter* handle);
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

#ifdef __cplusplus
}
#endif
