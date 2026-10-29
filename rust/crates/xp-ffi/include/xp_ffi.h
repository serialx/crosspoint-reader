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

// I18n: immutable, NUL-terminated pointers valid for the firmware lifetime.
// Unknown string IDs/names return "???"; invalid/omitted languages use English
// for strings/character sets. Selection ignores an invalid requested language.
// No allocation, logging or retained input pointers.
const char* xp_i18n_get(uint8_t language, uint16_t id);
uint8_t xp_i18n_select_language(uint8_t current, uint8_t requested);
const char* xp_i18n_language_name(uint8_t language);
const char* xp_i18n_character_set(uint8_t language);
// code is NULL or a readable NUL-terminated string, unchanged for the call.
// Case-sensitive lookup; unknown codes and NULL return English (0).
uint8_t xp_i18n_language_from_code(const char* code);

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
// Events 1..3: requested index entries / selector bytes / style count on OOM.
// 4: selector length, limit. 5..6: stored rule count on limit/OOM (after 1..3).
// 7: truncated parser input, once at stylesheet end. 8: invalid cache flags.
// 9: cache rule count, limit. 10: invalid selector length. 11: duplicate key.
// 12..13: new parser/cache scratch OOM (13's a is the LowMemory status).
// Unused a/b and all c/d are zero. No successful per-rule callbacks.
typedef enum {
  XP_CSS_INDEX_OOM = 1,
  XP_CSS_SELECTOR_OOM,
  XP_CSS_STYLE_OOM,
  XP_CSS_SELECTOR_LONG,
  XP_CSS_STORE_LIMIT,
  XP_CSS_STORE_OOM,
  XP_CSS_INPUT_TRUNCATED,
  XP_CSS_CACHE_FLAGS,
  XP_CSS_CACHE_COUNT,
  XP_CSS_CACHE_SELECTOR_LENGTH,
  XP_CSS_CACHE_DUPLICATE,
  XP_CSS_SCRATCH_OOM,
  XP_CSS_CACHE_SCRATCH_OOM
} XpCssDiagnosticEvent;
typedef struct {
  void* ctx;
  // Synchronous, nullable=silent; do not reenter Rust or retain pointers.
  void (*diagnostic)(void* ctx, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
} XpCssDiagnostics;

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
// Same contracts/results; diagnostics is null or borrowed unchanged for this call.
uint8_t xp_css_feed_with_diagnostics(XpCss* handle, const uint8_t* data, size_t len, uint8_t end,
                                     const XpCssDiagnostics* diagnostics);
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
typedef struct {
  uint32_t event, a, b;
} XpCssCacheDiagnostic;
// Same status/output contract. detail is null or writable, aligned, disjoint storage.
// event=0 means no diagnostic; otherwise uses the event/arguments above. No callback.
int32_t xp_css_cache_read_with_diagnostic(XpCss* handle, const uint8_t* data, size_t len, uint8_t end,
                                          XpCssCacheDiagnostic* detail);

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
#ifdef __cplusplus
enum class XpParaDiagnostic : uint32_t { NfcOffsets = 1, WordArena = 2, LineArena = 3, OtherStorage = 4 };
#endif
typedef struct XpParaAllocator {
  // word_chunk is 1 only for word-byte chunks; it affects diagnostics only.
  void* (*allocate)(size_t bytes, uint8_t word_chunk);
  void (*release)(void* data);
  /* Nullable, synchronous, no reentry. event 1: NFC chunk failure (a=bytes),
   * 2: word arena failure, 3: line extraction failure, 4: other storage failure
   * (a=bytes). Context is null; other arguments are zero. */
  void (*diagnostic)(void* context, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
#ifdef __cplusplus
      = nullptr
#endif
      ;
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
typedef struct XpNativeProvider XpNativeProvider;
// Callback backend: native is null; advance/space/kerning/space_advance/ascender,
// is_sd/starts_rtl/visual_order are required. Native backend: native fills a live
// provider and returns nonzero; those callbacks may be null. If native can return
// zero, the complete callback backend is required as fallback. Both require prepare;
// trace is optional. engine is reserved for Rust and must be null from C++.
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
  uint8_t (*native)(void*, XpNativeProvider*);
  const void* engine;  // Internal synchronous Rust layout context; C++ initializes null.
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
// Borrowed only during the call. Null options/callback means silent. Callbacks
// must not reenter Rust or retain text. Integers c/d are reserved and zero.
// Event arguments (a,b; text,other): NAV_BEGIN/END and GUIDE_BEGIN: none;
// METADATA_TRUNCATED/COLLECTION_IGNORED: limit; COVER_IGNORED: item id, media type;
// NCX_DUPLICATE/NAV_DOCUMENT/GUIDE_START/GUIDE_COVER: href;
// MANIFEST_INDEX/INDEX_OOM/INDEX_READY: count; SPINE_CLOSED/TOC_CLOSED: none;
// MISSING_TOC_HREF: href; MISSING_SPINE_TITLE: zero-based spine index, href.
// All unused integers/spans are zero/empty. Text is not NUL-terminated.
enum {
  XP_PKG_DIAG_NAV_BEGIN = 1,
  XP_PKG_DIAG_NAV_END,
  XP_PKG_DIAG_METADATA_TRUNCATED,
  XP_PKG_DIAG_COLLECTION_IGNORED,
  XP_PKG_DIAG_COVER_IGNORED,
  XP_PKG_DIAG_NCX_DUPLICATE,
  XP_PKG_DIAG_NAV_DOCUMENT,
  XP_PKG_DIAG_GUIDE_START,
  XP_PKG_DIAG_GUIDE_COVER,
  XP_PKG_DIAG_MANIFEST_INDEX,
  XP_PKG_DIAG_GUIDE_BEGIN,
  XP_PKG_DIAG_INDEX_OOM = 20,
  XP_PKG_DIAG_INDEX_READY,
  XP_PKG_DIAG_SPINE_CLOSED,
  XP_PKG_DIAG_TOC_CLOSED,
  XP_PKG_DIAG_MISSING_TOC_HREF,
  XP_PKG_DIAG_MISSING_SPINE_TITLE
};
typedef struct {
  void* context;
  void (*diagnostic)(void*, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d, const uint8_t* text,
                     uint32_t text_len, const uint8_t* other, uint32_t other_len);
} XpPkgDiagnostics;
// Optional write/finish result, reset on entry. code is the XML error code;
// line is one-based, or zero when unavailable; buffer identifies input-buffer
// allocation failure. A non-null result must be writable and not alias the
// parser, input, or options. Non-null options must be readable through return.
typedef struct {
  uint32_t code;
  uint32_t line;
  uint8_t buffer;
} XpPkgError;
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
size_t xp_pkg_pass_write(XpPkgPass*, XpPkgSpan, const XpPkgDiagnostics*, XpPkgError*);
uint8_t xp_pkg_pass_finish(XpPkgPass*, const XpPkgDiagnostics*, XpPkgError*);
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
int32_t xp_pkg_cache_control(XpPkgCache*, const XpPkgIo*, uint32_t operation, const XpPkgDiagnostics*);
// kind 0 writes a spine href; kind 1 writes title/href/anchor/level for a TOC entry.
int32_t xp_pkg_cache_entry(XpPkgCache*, const XpPkgIo*, uint32_t kind, XpPkgSpan title, XpPkgSpan href,
                           XpPkgSpan anchor, uint8_t level, const XpPkgDiagnostics*);
// Metadata order: title, author, language, cover href, text-reference href.
// build flushes/closes all opened build streams and removes incomplete book output
// after record/flush failure; temporary stores remain caller-owned.
int32_t xp_pkg_cache_build(XpPkgCache*, const XpPkgIo*, const XpPkgSpan metadata[5], const XpPkgDiagnostics*);
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
  uint8_t href[256];
  int16_t x, y, width, height;
} XpChapterLink;
typedef struct {
  uint16_t paragraph, list;
  uint32_t visible;
  int16_t next_y;
} XpChapterPosition;
/* Diagnostic callbacks are nullable and synchronous: no reentry or retained text.
 * Failure events 1-13 carry zero arguments. SoftFlush: a=word count.
 * NestedTable: zero arguments. ImageCss*: a/b=signed pixel width/height.
 * ImageScale: a/b=pixels, c=IEEE-754 float scale bits. DroppedLink: borrowed href.
 * d and unused arguments are zero. No successful word/line callback is emitted. */
#ifdef __cplusplus
enum class XpChapterDiagnostic : uint32_t {
  BorderBox = 1,
  DropCap = 2,
  Paragraph = 3,
  HorizontalRule = 4,
  RowSeparator = 5,
  CellBorder = 6,
  TableCell = 7,
  ImageBlock = 8,
  PageImage = 9,
  CharacterParagraph = 10,
  AfterTableParagraph = 11,
  PageLine = 12,
  NoParagraph = 13,
  SoftFlush = 14,
  NestedTable = 15,
  ImageCssBoth = 16,
  ImageCssHeight = 17,
  ImageCssWidth = 18,
  ImageScale = 19,
  DroppedLink = 20,
};
#endif
/* Borrowed sink/metrics/context outlive the chapter. All spans are borrowed
 * synchronously. Only measure_table may use its supplied state pointer. */
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
  int32_t (*font_ready)(void* context, int32_t font, int32_t fallback);
  /* One complete encoded page. 0 = success, 1 = stop, other = failure.
   * The callback may set its stop latch, but cannot reenter/destroy the parser. */
  uint8_t (*complete)(void* context, XpChapterSpan page, XpChapterPosition position);
  /* Ready paths are borrowed by image_paths after the intervening text flush. */
  uint8_t (*probe_image)(void* context, XpChapterSpan source, int32_t* counter, bool* popup_shown, uint16_t* width,
                         uint16_t* height);
  void (*image_paths)(void* context, XpChapterSpan* cache, XpChapterSpan* source);
  XpChapterSpan (*pending_anchor)(void* context);
  void (*defer_anchor)(void* context, XpChapterSpan id);
  uint8_t (*anchor_is_toc)(void* context, XpChapterSpan id);
  uint32_t (*anchor_count)(void* context);
  void (*record_anchor)(void* context, uint16_t page);
  void (*measure_table)(void* context, XpChapterState* state);
  void (*trace)(void* context, const XpChapterTrace* observation);
  void (*trace_bytes)(void* context, XpChapterSpan bytes);
  void (*diagnostic)(void* context, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d, const uint8_t* text,
                     uint32_t text_len);
} XpChapterSink;
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
/* Packed feed result: bits 0..7 = More/Done/Error, 8..31 = Expat error,
 * 32..63 = one-based line. Error/line are zero without an XML error. */
uint64_t xp_chapter_feed_diagnostic(XpChapter*, const uint8_t*, uint32_t, uint8_t);
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
// Shared image events also travel through XpPngServices::diagnostic. All calls
// are synchronous; nullable callbacks are silent and must not reenter Rust.
// Unlisted arguments are zero. Dimension pairs put width in low 16 bits.
typedef enum {
  XP_IMAGE_DIAG_CacheBand = 256,         // a/b: available/required rows; c/d: width/height.
  XP_IMAGE_DIAG_CacheAllocation = 257,   // a: requested bytes; failed band allocation.
  XP_IMAGE_DIAG_CacheOpen = 258,         // Cache open failed.
  XP_IMAGE_DIAG_CacheHeader = 259,       // Either two-byte header write failed.
  XP_IMAGE_DIAG_CacheStarted = 260,      // a/b: width/height; c: band rows; header written.
  XP_IMAGE_DIAG_CacheRow = 261,          // a: image-local row; advance or final write failed.
  XP_IMAGE_DIAG_CacheClose = 262,        // Cache close failed.
  XP_IMAGE_DIAG_CacheWritten = 263,      // a/b: width/height; c: total bytes; close succeeded.
  XP_IMAGE_DIAG_OutputDimensions = 264,  // a: kind; b/c: signed degenerate output width/height.
  XP_IMAGE_DIAG_RowAllocation = 265,     // a: kind (0 PNG, 2 GIF); b: failed row-buffer bytes.
  XP_IMAGE_DIAG_Configuration = 266,     // Other pipeline configuration rejection.
  XP_IMAGE_DIAG_PngSummary = 267,        // a/b/c: source/visible/output pairs; d: f32 scale bits.
  XP_IMAGE_DIAG_JpegSummary = 268,       // a: source w[0:14], h[15:29], log2 denominator[30:31];
                                    // b: output w[0:14], h[16:30], progressive[31]; c/d: f32 scale/fine-scale bits.
} XpImageDiagnostic;
typedef struct {
  void* context;
  void (*target)(void*, XpImageTarget*);
  bool (*io)(void*, uint32_t operation, const uint8_t*, size_t);
  void (*diagnostic)(void*, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
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

// GIF first-frame decoding. Input/output records and their contexts are borrowed
// synchronously; callbacks must not reenter. read writes at most len bytes and
// returns the number read (zero on failure/EOF). poll may yield, but must preserve
// all borrowed memory. No input or output span is retained after a callback.
// The image target/IO contract is the same as xp_image_init. All allocations are
// fallible, decode-local Rust owners. False reports malformed input, OOM or IO.
typedef struct {
  uint16_t canvasWidth, canvasHeight, frameX, frameY, frameWidth, frameHeight;
  uint8_t interlaced;
} XpGifInfo;
/* GIF diagnostic protocol: optional, synchronous, no retained pointers. The callback
 * must not reenter Rust or perform I/O on the borrowed input/output. Unlisted a-d
 * arguments are zero. Packed pairs are low 16 = width/x, high 16 = height/y, pixels.
 * Read-failure events observe the immediately preceding failed I/O before another
 * I/O operation; adapters may inspect their last seek result. Specific errors may
 * be followed by consumer summary errors, in call order (not a unique-error stream).
 * DecodeStart precedes decoder reads; FrameEnd is advisory, not successful output.
 * FrameEnd uses buffered state only (zero diagnostic reads), not a parsed frame end
 * or proof of animation. Compare a < file_size - 10 only when file_size > 10.
 * BMP consumers defer the animation message until Complete, before completion text;
 * framebuffer consumers may report it immediately. Postprocess excludes Complete.
 */
typedef enum {
  XP_GIF_DIAG_BlockLength = 1,        // Sub-block length read failed; a = previous block payload bytes.
  XP_GIF_DIAG_BlockSkip = 2,          // Reserved (not emitted); a = skipped payload bytes.
  XP_GIF_DIAG_HeaderRead = 3,         // Header read failed (including seek failure).
  XP_GIF_DIAG_Signature = 4,          // Invalid GIF signature.
  XP_GIF_DIAG_MarkerRead = 5,         // Marker read failed; a = 1 at global palette end, otherwise 0.
  XP_GIF_DIAG_Trailer = 6,            // Trailer before first descriptor; a = trailer byte (0x3b).
  XP_GIF_DIAG_ExtensionLabel = 7,     // Extension label read failed.
  XP_GIF_DIAG_ExtensionData = 8,      // Extension sub-block scan failed; follows BlockLength when applicable.
  XP_GIF_DIAG_Descriptor = 9,         // First image descriptor read failed.
  XP_GIF_DIAG_CodeSize = 10,          // Missing/invalid LZW minimum code size.
  XP_GIF_DIAG_Marker = 11,            // Unexpected block marker; a = marker byte.
  XP_GIF_DIAG_InvalidHeader = 12,     // Consumer header inspection failed; follows parser events.
  XP_GIF_DIAG_InterlaceBmp = 13,      // BMP rejected interlaced input before output/allocation.
  XP_GIF_DIAG_Dimensions = 14,        // BMP rejected canvas size; a/b = width/height in pixels.
  XP_GIF_DIAG_OutputDimensions = 15,  // BMP rejected target size; a/b = int32 width/height bits, pixels.
  XP_GIF_DIAG_OutputLimit = 16,       // BMP byte-size overflow before writing header.
  XP_GIF_DIAG_BmpHeader = 17,         // BMP header output callback failed.
  XP_GIF_DIAG_BmpRows = 18,           // BMP row-buffer allocation failed.
  XP_GIF_DIAG_Scaling = 19,           // BMP scaling-accumulator allocation failed.
  XP_GIF_DIAG_Atkinson1 = 20,         // BMP 1-bit dither-buffer allocation failed.
  XP_GIF_DIAG_Atkinson2 = 21,         // BMP 2-bit dither-buffer allocation failed.
  XP_GIF_DIAG_DecoderAlloc = 22,      // LZW table or workspace allocation failed; no Decode summary.
  XP_GIF_DIAG_Decode = 23,            // Frame decode/output failed; a/b = framebuffer canvas pixels (0 for BMP).
  XP_GIF_DIAG_Postprocess = 24,       // BMP gap-row post-processing failed after successful decode.
  XP_GIF_DIAG_Complete = 25,          // BMP finished successfully, after all output writes.
  XP_GIF_DIAG_FrameHeader = 26,       // Validated frame setup; a=canvas, b=frame, c=offset packed pairs; d=interlaced.
  XP_GIF_DIAG_CanvasWidth = 27,       // Framebuffer canvas exceeds decoder width; a/b = canvas pixels.
  XP_GIF_DIAG_Cache = 28,             // Framebuffer cache start failed; conversion fails.
  XP_GIF_DIAG_Finish = 29,            // a: 1 for gap post-processing failure, 0 for cache finalization failure.
  XP_GIF_DIAG_Pipeline = 30,          // Reserved legacy rejection event; now XP_IMAGE_DIAG_Configuration.
  XP_GIF_DIAG_PipelineAlloc = 31,     // Reserved legacy row-allocation event; now XP_IMAGE_DIAG_RowAllocation.
  XP_GIF_DIAG_Open = 32,              // Unsupported extension; a/b = framebuffer canvas pixels (0 for BMP).
  XP_GIF_DIAG_Interlace = 33,         // Framebuffer rejected interlaced input before pipeline creation.
  XP_GIF_DIAG_FrameDimensions = 34,   // Framebuffer rejected canvas size; a/b = width/height pixels.
  XP_GIF_DIAG_FrameEnd = 35,          // Decode stopped at EOI/completed rows; a = logical read-ahead byte offset.
  XP_GIF_DIAG_DecodeStart = 36,       // Consumer starts decoding; sets facade read/seek-error logging state.
} XpGifDiagnostic;
typedef struct {
  void* context;
  size_t (*read)(void*, uint32_t offset, uint8_t* output, size_t len);
  void (*poll)(void*);
  // Optional; synchronous and non-reentrant. No retained pointers.
  void (*diagnostic)(void*, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
} XpGifInput;
typedef struct {
  int32_t width, height;
  uint8_t one_bit, crop, original_thresholds;
} XpGifBmpConfig;
typedef struct {
  void* context;
  bool (*write)(void*, const uint8_t*, size_t);
} XpGifOutput;
// inspect clears info, then fills fields as they are parsed (including on failure).
bool xp_gif_inspect(const XpGifInput*, XpGifInfo*);
bool xp_gif_frame(const XpGifInput*, const XpImageConfig*, const XpImageCallbacks*, bool cache);
bool xp_gif_bmp(const XpGifInput*, const XpGifBmpConfig*, const XpGifOutput*);

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
// Host differential-test adapter for text measurement. Firmware uses FontFace directly.
// All callbacks are required and synchronous. The table and context outlive the call;
// callbacks may mutate only context-owned font/cache storage, never the table or input,
// and must not reenter measurement. No pointer is retained. A null face means absent.
// Codepoint arrays are aligned/readable for count entries, length <= ISIZE_MAX/4;
// null is allowed only at count zero. NUL ends the stream. Ascenders and metrics
// obey EpdFontData ranges; compression is finite and its result fits int32_t.
// Visual streams have already been reordered/shaped once; resolve_font takes logical text.
typedef struct {
  uint16_t advance;
  uint8_t width, height;
  int16_t left, top;
} XpTextmGlyph;
typedef struct {
  void* context;
  uint8_t (*glyph)(void*, uint32_t, uint8_t, XpTextmGlyph*);
  uint8_t (*coverage)(void*, uint32_t, uint8_t);
  int8_t (*kern)(void*, uint32_t, uint32_t, uint8_t);
  uint32_t (*ligature)(void*, uint32_t, uint32_t, uint8_t);
  uint8_t (*has_table)(void*);
  uint8_t (*resolve_style)(void*, uint8_t);
  uint16_t (*advance)(void*, uint32_t, uint8_t);
  int32_t (*ascender)(void*, uint8_t);
  uint8_t (*line_height)(void*);
  uint8_t (*transparent)(uint32_t);
} XpTextmFace;
typedef struct {
  void* context;
  uint8_t (*fallback)(void*, int32_t, int32_t*);
  uint8_t (*contains)(void*, int32_t);
  uint8_t (*coverage)(void*, int32_t, uint32_t, uint8_t);
} XpTextmRegistry;
int32_t xp_textm_advance(const XpTextmFace*, const uint32_t*, uint32_t, uint8_t, int8_t, uint8_t layout);
int32_t xp_textm_width(const XpTextmFace*, const uint32_t*, uint32_t, uint8_t);
int32_t xp_textm_space_width(const XpTextmFace*, uint8_t);
int32_t xp_textm_space_advance(const XpTextmFace*, uint32_t, uint32_t, uint8_t);
int32_t xp_textm_kerning(const XpTextmFace*, uint32_t, uint32_t, uint8_t, int8_t);
// On miss, both output integers are unchanged. Outputs must be writable and distinct.
uint8_t xp_textm_codepoint(const XpTextmFace*, uint32_t, uint8_t, int32_t*, int32_t*);
int32_t xp_textm_ascender(const XpTextmFace*);
int32_t xp_textm_line_height(const XpTextmFace*, float);
int32_t xp_textm_resolve_font(const XpTextmRegistry*, int32_t, const uint32_t*, uint32_t, uint8_t);
// Native font tables (test ABI; no firmware callers yet).
// data points to an EpdFontData with valid sorted tables and in-range glyph,
// class and CSR indices. All pointers/counts describe immutable live memory
// for the call, including optional packed arrays (alignment 1). No function
// invokes callbacks, loads storage, allocates, retains or frees memory.
// Rebuild views after cache mutation; do not mutate/reenter while borrowed.
// Outputs must be non-null and disjoint from inputs. A failed/absent lookup
// leaves its output untouched. All advances/kerns retain their raw fp4 units.
typedef struct {
  uint8_t width, height;
  uint16_t advanceX;
  int16_t left, top;
  uint16_t dataLength;
  uint32_t dataOffset;
} XpFontGlyph;

typedef struct {
  const void* data;      // Resolved style's EpdFontData, non-null.
  const void* advances;  // SdCardFont::AdvanceEntry[advanceCount].
  uint32_t advanceCount;
  const void* fullIntervals;  // EpdUnicodeInterval[coverageCount], unless compact.
  const void* bmpIntervals;   // Packed { uint16 first, last, offset }[coverageCount].
  uint32_t coverageCount;     // Complete coverage; not just the mini glyph set.
} XpFontSdView;

enum { XP_FONT_MISSING = 0, XP_FONT_RESIDENT = 1, XP_FONT_SYNTHETIC = 2, XP_FONT_NOT_RESIDENT = 3 };
uint32_t xp_font_glyph(const void* data, uint32_t cp, bool metrics, XpFontGlyph* out);
uint32_t xp_font_sd_glyph(const XpFontSdView* view, uint32_t cp, bool metrics, XpFontGlyph* out);
// -1 means a coverage callback would be needed, 0 absent, 1 covered.
int32_t xp_font_has_codepoint(const void* data, uint32_t cp);
bool xp_font_sd_has_codepoint(const XpFontSdView* view, uint32_t cp);
// True includes a resident zero advance; false means not in the advance table.
bool xp_font_sd_advance(const XpFontSdView* view, uint32_t cp, uint16_t* out);
// False means the dynamic kernHandler is required; CJK pairs return true/zero first.
bool xp_font_kerning(const void* data, uint32_t left, uint32_t right, int8_t* out);
uint32_t xp_font_ligature(const void* data, uint32_t left, uint32_t right);
// text is len readable bytes (null allowed only for zero len); consumed is bytes.
uint32_t xp_font_apply_ligatures(const void* data, uint32_t cp, const uint8_t* text, size_t len, size_t* consumed);

// Native measurement: synchronous, serialized loans of renderer-owned tables.
// Every table or registration mutation increments generation. Setup fills all
// styles; glyph/kern/coverage callbacks run only after native table loans end.
typedef struct {
  XpFontSdView styles[4];
  int32_t fallback;
  uint8_t hasFallback, sd, advanceTable, resolved[4];
} XpNativeFamily;
// Failure-only, synchronous diagnostics. MissingFont: a is the signed font ID
// bit pattern; BidiLimit: a is the codepoint limit (128). b/c/d are zero.
// NULL is silent. The callback must not reenter Rust or retain borrowed pointers.
#ifdef __cplusplus
enum class XpNativeDiagnostic : uint32_t { MissingFont = 1, BidiLimit = 2 };
#endif
typedef void (*XpNativeDiagnosticFn)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
struct XpNativeProvider {
  void* context;
  const void* generation;  // Atomic uint32_t, same size/alignment as Rust AtomicU32.
  void (*family)(void*, int32_t, XpNativeFamily*);
  uint8_t (*glyph)(void*, int32_t, uint32_t, uint8_t, XpTextmGlyph*);
  uint8_t (*coverage)(void*, int32_t, uint32_t, uint8_t);
  int8_t (*kern)(void*, int32_t, uint32_t, uint32_t, uint8_t);
  void* (*bidiAcquire)();
  void (*bidiRelease)();
  void (*prewarm)(void*, int32_t, const uint8_t*, size_t, uint8_t);
  XpNativeDiagnosticFn diagnostic;
};
// Borrow the existing initialized bidi workspace exclusively for these calls.
// Visitor pointers are valid only during the callback; callbacks cannot reenter bidi.
#ifdef __cplusplus
enum class XpBidiVisitStatus : uint8_t { Unchanged, Applied, TooLong };
#else
typedef uint8_t XpBidiVisitStatus;
#endif
XpBidiVisitStatus xp_bidi_visual_visit(const uint8_t*, size_t, int32_t, void*, void*,
                                       void (*)(void*, const uint8_t*, size_t));
// Each oversized token reports BidiLimit before visiting the next token.
void xp_bidi_rtl_tokens(const uint8_t*, size_t, void*, void*, void (*)(void*, const uint8_t*, size_t),
                        XpNativeDiagnosticFn);
int32_t xp_bidi_words(size_t, const void*, const uint8_t* (*)(const void*, size_t, size_t*), uint8_t, void*, uint16_t*);
// The resident pointer expires before any font-owner mutation or next bitmap request.
const void* xp_font_resident_glyph(const void*, uint32_t);
void xp_font_solid_metrics(uint32_t, int32_t, uint16_t, XpFontGlyph*);
void xp_textm_dimensions_utf8(const XpTextmFace*, const uint8_t*, size_t, int32_t*, int32_t*);
int32_t xp_native_draw_scaled(const XpNativeProvider*, int32_t, uint32_t, uint8_t, int32_t, void*, void (*)(void*));
// Synchronous glyph emission: callbacks cannot reenter bidi, register/unregister
// fonts, or retain pointers. prepare receives terminated visual text and returns
// a null slot for a cache scan. A returned data slot belongs to the stable EpdFont
// owner and is reread after every callback. Each placement is consumed before the next query.
typedef struct {
  uint32_t codepoint;
  const void* glyph;  // Optional borrowed EpdGlyph; valid only during emission.
  int32_t x, y;
  uint8_t scaleNum, scaleDen;
} XpGlyphPlacement;
void xp_native_draw(const XpNativeProvider*, int32_t, const uint8_t*, size_t, uint8_t, int8_t, int32_t, uint8_t, void*,
                    const void* const* (*)(void*, const uint8_t*, size_t), void (*)(void*, const XpGlyphPlacement*));
int32_t xp_native_advance(const XpNativeProvider*, int32_t, const uint8_t*, size_t, uint8_t, int8_t, int32_t, uint8_t);
int32_t xp_native_width(const XpNativeProvider*, int32_t, const uint8_t*, size_t, uint8_t, int32_t);
int32_t xp_native_resolve(const XpNativeProvider*, int32_t, const uint8_t*, size_t, uint8_t);
int32_t xp_native_space(const XpNativeProvider*, int32_t, uint8_t);
int32_t xp_native_gap(const XpNativeProvider*, int32_t, uint32_t, uint32_t, uint8_t);
int32_t xp_native_kern(const XpNativeProvider*, int32_t, uint32_t, uint32_t, uint8_t, int8_t);
uint8_t xp_native_codepoint(const XpNativeProvider*, int32_t, uint32_t, uint8_t, int32_t*, int32_t*);
void* xp_native_bidi_acquire();
void xp_native_bidi_release();

// Glyph rasterization. Numeric orientation/plane values
// match GfxRenderer. Borrow all memory for one synchronous call, with no aliasing.
typedef struct {
  int32_t left, top, right, bottom;
} XpGlyphClip;
typedef struct {
  uint8_t* buffer;
  uint32_t bufferLen;
  int32_t width, height, stride, originY, rows;
  XpGlyphClip clip;
  uint8_t orientation, plane, absolute, strip, scanning;
} XpGlyphTarget;
typedef struct {
  uint8_t width, height;
  uint16_t advanceX;
  int16_t left, top;
  uint16_t dataLength;
  uint32_t dataOffset;
} XpRasterGlyph;
typedef enum { XP_GLYPH_NORMAL, XP_GLYPH_ROTATED, XP_GLYPH_COVERAGE, XP_GLYPH_BILINEAR } XpGlyphKind;
typedef struct {
  XpRasterGlyph glyph;
  int32_t x, y, ascender, scaleNum, scaleDen, scale256;
  uint8_t kind, twoBit, state, solid;
} XpGlyphDraw;
typedef struct {
  uint32_t glyphIndex, bufferOffset, alignedOffset;
} XpGlyphPageEntry;
typedef struct {
  const uint8_t* buffer;
  const void* font;
  const XpGlyphPageEntry* entries;
  uint16_t count;
} XpGlyphPageSlot;
// Generation is a live std::atomic<uint32_t>. Rebuild on any table/page-buffer
// mutation, including FontDecompressor prewarm/clear. No view or
// bitmap pointer may survive an owner callback. Page slots mirror the native
// layout; their valid offsets and glyph sizes must fit their borrowed buffers.
typedef struct {
  const void* generation;
  uint32_t capturedGeneration;
  const void* font;
  uint32_t glyphCount, bitmapLen;
  const XpGlyphPageSlot* slots;
  uint32_t slotCount;
  const void* overflowFirst;
  uint32_t overflowStride, overflowCount;
} XpGlyphSource;
typedef struct {
  const uint8_t* data;
  uint32_t len;
} XpGlyphBitmap;
// Draw returns 0 for invalid descriptors, 1 for a completed (possibly clipped) draw.
// Glyph dimensions are u8; coordinates/clip are bounded to +/-1,000,000;
// coverage ratios use positive components <=256, bilinear scale256 is 1..4096.
uint8_t xp_glyph_draw(const XpGlyphTarget*, const XpGlyphDraw*, const uint8_t*, uint32_t);
uint8_t xp_glyph_intersects_strip(const XpGlyphTarget*, int32_t, int32_t, int32_t, int32_t);
// Returns 1 for resident bytes (including an empty glyph), 0 for a miss.
// TTF, SD overflow, hot-group and stale-view misses stay with the C++ owner.
uint8_t xp_glyph_bitmap(const XpGlyphSource*, const void* glyph, XpGlyphBitmap*);
// Native draws trust the renderer target: it satisfies xp_glyph_draw geometry
// bounds, owns bufferLen writable bytes, and stays immutable during callbacks.
// Miss callbacks run without table or bitmap loans, refill source, and return
// bytes consumed before the next
// owner callback. Font registration and bidi reentry are forbidden.
typedef struct {
  XpGlyphTarget target;
  XpGlyphSource source;
  int32_t x, y;
  uint8_t state;
} XpGlyphPaint;
// Miss may update only draw.glyph, ascender, twoBit, solid and the source view.
typedef XpGlyphBitmap (*XpGlyphMiss)(void*, uint32_t, const void*, XpGlyphDraw*, XpGlyphSource*);
void xp_native_draw_pixels(const XpNativeProvider*, int32_t, const uint8_t*, size_t, uint8_t, int8_t, int32_t, uint8_t,
                           void*, const void* const* (*)(void*, const uint8_t*, size_t), XpGlyphPaint*, XpGlyphMiss);
int32_t xp_native_draw_scaled_pixels(const XpNativeProvider*, int32_t, uint32_t, uint8_t, int32_t, const void* const*,
                                     void*, XpGlyphPaint*, XpGlyphMiss);
#ifdef XP_FFI_HOST_FAULT_INJECTION
void xp_native_draw_pixels_traced(const XpNativeProvider*, int32_t, const uint8_t*, size_t, uint8_t, int8_t, int32_t,
                                  uint8_t, void*, const void* const* (*)(void*, const uint8_t*, size_t), XpGlyphPaint*,
                                  XpGlyphMiss, void (*)(void*, const XpGlyphPlacement*));
#endif

// Page painting borrows a decoded arena and the existing framebuffer for one pass.
// Callbacks are synchronous, retain no input, and must not reenter page drawing.
// Source slots remain stable until return; table/bitmap loans end before owner callbacks.
// The target obeys xp_native_draw_pixels geometry and exclusive-buffer requirements.
// scan receives NUL-terminated visual text, batched within each line/font/style.
// resident receives a bounded visual slice (not necessarily NUL-terminated).
// mode: 0 all, 1 images only, 2 image placeholders. Returns 0 on success.
typedef struct {
  void* context;
  uint8_t (*ensure)(void*, int32_t);
  const void* const* (*source)(void*, int32_t, uint8_t, XpGlyphSource*);
  void (*resident)(void*, int32_t, uint8_t, const uint8_t*, size_t);
  void (*scan)(void*, int32_t, uint8_t, const uint8_t*);
  void (*image)(void*, uint16_t, int32_t, int32_t, uint8_t);
  XpGlyphMiss miss;
} XpPageRenderServices;
uint32_t xp_page_render(const uint8_t*, size_t, const XpNativeProvider*, const XpPageRenderServices*, XpGlyphPaint*,
                        int32_t font, int32_t x, int32_t y, uint8_t mode);
#ifdef XP_FFI_HOST_FAULT_INJECTION
uint32_t xp_page_render_traced(const uint8_t*, size_t, const XpNativeProvider*, const XpPageRenderServices*,
                               XpGlyphPaint*, int32_t, int32_t, int32_t, uint8_t,
                               void (*)(void*, const XpGlyphPlacement*));
#endif

// SD font v4 parsing. No allocation, I/O, callbacks or retained pointers.
// Byte spans are readable for len bytes (len <= PTRDIFF_MAX), NULL only if empty.
// Typed outputs/state are live, aligned and exclusive for the call. State is
// initialized by header (Directory) or {0, 0, glyphCount <= UINT16_MAX} (Intervals).
// Input/output/state do not overlap, except interval output may alias its bytes.
// False leaves outputs/state unchanged. Malformed bytes never panic.
// Offsets retain uint32 wrap; table I/O and bitmap extent checks remain deferred.
typedef struct {
  uint32_t intervalCount, glyphCount;
  uint8_t advanceY;
  int16_t ascender, descender;
  bool is2Bit;
  uint16_t kernLeftEntryCount, kernRightEntryCount;
  uint8_t kernLeftClassCount, kernRightClassCount, ligaturePairCount;
} XpSdfontHeader;
typedef struct {
  uint32_t hash;
  uint8_t styleCount;
  bool is2Bit;
  uint8_t seen;
} XpSdfontDirectory;
typedef struct {
  XpSdfontHeader header;
  uint32_t offsets[7];  // intervals, glyphs, left/right kern, matrix, ligatures, bitmap
  uint8_t id;
} XpSdfontStyle;
typedef struct {
  uint32_t expectedOffset, previousLast;
  bool bmp16;
} XpSdfontIntervals;
/* SD-font diagnostic protocol: optional synchronous callback; no reentry to cache
 * operations or I/O on its borrowed handles. Unlisted a-d arguments are zero;
 * counts/indices are unsigned, indices are zero-based; style is 0..3 in slot order.
 * Read-failure events precede any subsequent I/O so the adapter's last seek result
 * still describes that failure. Events follow execution order across retries and
 * styles, so multiple failures or progress events can occur per call. Allocation
 * failures can trigger fallbacks; a diagnostic alone does not specify call status.
 * Loaded/Built events follow the named operation; retry events mark the transition
 * described below. No pointers, strings, or heap ownership cross this callback.
 */
typedef enum {
  XP_SDFONT_DIAG_LigatureOpen = 1,     // Ligature file open failed.
  XP_SDFONT_DIAG_LigatureAlloc = 2,    // Ligature pair allocation failed.
  XP_SDFONT_DIAG_LigatureRead = 3,     // Ligature seek/read failed.
  XP_SDFONT_DIAG_LigaturesLoaded = 4,  // Pairs loaded; a = pair count.
  XP_SDFONT_DIAG_KernOpen = 5,         // Mini-kern file open failed.
  XP_SDFONT_DIAG_KernScratch = 6,      // Mini-kern scratch allocation failed; a = requested bytes.
  XP_SDFONT_DIAG_KernClasses = 7,      // Kern class seek/read failed.
  XP_SDFONT_DIAG_KernAlloc = 8,        // Mini-kern allocation failed; a/b/c = left/right/matrix requested bytes.
  XP_SDFONT_DIAG_KernRow = 9,          // Kern row seek/read failed; a = original row index (zero-based).
  XP_SDFONT_DIAG_KernBuilt = 10,       // Mini-kern built; a/b = new left/right counts, c/d = full counts.
  XP_SDFONT_DIAG_Codepoints = 11,      // Prewarm codepoint allocation failed; a = requested bytes.
  XP_SDFONT_DIAG_ArenaRetry =
      12,  // Before smaller prewarm retry; a/b = old/new glyph count, c = bytes/glyph, d = largest free bytes.
  XP_SDFONT_DIAG_ResidentCodepoints = 13,  // Resident kern codepoints allocation failed; a = count, b = style.
  XP_SDFONT_DIAG_Underuse = 14,            // Before releasing underused mini cache; a/b = used/capacity bytes.
  XP_SDFONT_DIAG_Mapping = 15,             // Prewarm mapping allocation failed; a = style.
  XP_SDFONT_DIAG_Intervals = 16,           // Mini interval allocation failed; a = style.
  XP_SDFONT_DIAG_Glyphs = 17,              // Mini glyph allocation failed; a = style.
  XP_SDFONT_DIAG_ReadOrder = 18,           // Prewarm read-order allocation failed; a = style.
  XP_SDFONT_DIAG_PrewarmOpen = 19,         // Prewarm file reopen failed; a = style.
  XP_SDFONT_DIAG_PrewarmGlyph = 20,        // Prewarm glyph seek/read failed; a = glyph index, b = style.
  XP_SDFONT_DIAG_RetryBitmap = 21,  // After advance-cache release, before retry; a/b = largest free bytes before/after.
  XP_SDFONT_DIAG_ReserveBitmap = 22,   // Bitmap reservation failed; a = requested bytes, b = style.
  XP_SDFONT_DIAG_ReservedBitmap = 23,  // Bitmap reserved before metadata rebuild; a = bytes.
  XP_SDFONT_DIAG_Bitmap = 24,          // Mini bitmap allocation failed; a = requested bytes, b = style.
  XP_SDFONT_DIAG_BitmapOrder = 25,     // Bitmap read-order allocation failed; a = style.
  XP_SDFONT_DIAG_PrewarmBitmap = 26,   // Prewarm bitmap seek/read failed; a = style.
  XP_SDFONT_DIAG_AdvanceMerge = 27,    // Advance-table merge allocation failed; a = requested entries, b = style.
  XP_SDFONT_DIAG_AdvanceMapping = 28,  // Advance mapping allocation failed; a = style.
  XP_SDFONT_DIAG_AdvanceOpen = 29,     // Advance-table file open failed; a = style.
  XP_SDFONT_DIAG_AdvanceStaging = 30,  // Advance staging allocation failed; a = style.
  XP_SDFONT_DIAG_AdvanceGlyph = 31,    // Advance glyph seek/read failed; a = glyph index, b = style.
  XP_SDFONT_DIAG_AdvanceLoaded =
      32,  // After merge attempt; a = style, b = fetched entries, c = total entries, d = entry limit.
  XP_SDFONT_DIAG_AdvanceGrow = 33,        // Optional advance-table pre-grow failed; a = style; merge can retry.
  XP_SDFONT_DIAG_AdvanceCodepoints = 34,  // Advance codepoint allocation failed; a = requested bytes.
  XP_SDFONT_DIAG_OverflowOpen = 35,       // Overflow file open failed.
  XP_SDFONT_DIAG_OverflowGlyph = 36,      // Overflow metadata seek/read failed; a = Unicode codepoint, b = style.
  XP_SDFONT_DIAG_OverflowAlloc = 37,      // Overflow bitmap allocation failed; a = requested bytes, b = codepoint.
  XP_SDFONT_DIAG_OverflowBitmap = 38,     // Overflow bitmap seek/read failed; a = Unicode codepoint.
  XP_SDFONT_DIAG_OverflowLoaded =
      39,  // Overflow slot loaded; a = codepoint, b = style, c = slot index, d = slot count.
} XpSdfontDiagnostic;
// Synchronous, exclusive cache loans. Slots retain their addresses until unload.
// Resident buffers use the paired PSRAM allocator; scratch uses ordinary arrays.
// File callbacks are nested at most twice and may not reenter cache operations.
// close(false) closes the file; close(true) also releases its HAL handle.
typedef struct {
  void* context;
  bool (*open)(void*);
  void (*close)(void*, bool release);
  bool (*read)(void*, uint32_t offset, uint8_t* destination, size_t length, bool seek);
  uint8_t* (*alloc)(size_t bytes, bool resident);
  void (*free)(uint8_t*, bool resident);
  uint32_t (*heap)(bool largest);
  uint32_t (*clock)(void);
  void (*corrupt)(void*, const void* glyph, uint32_t codepoint);
  // Optional; synchronous and non-reentrant. Integer arguments are event-specific.
  void (*diagnostic)(void*, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
} XpSdfontServices;
typedef struct {
  void* styles;
  const uint8_t* slots;
  void** advances;
  uint32_t* advanceSizes;
  uint32_t* advanceCapacities;
  void* overflow;
  uint32_t* overflowCount;
  uint32_t* overflowNext;
  void* stats;
  uint32_t fileSize;
  bool loaded;
  const XpSdfontServices* services;
} XpSdfontPool;
typedef struct {
  const char* const* segments;
  const size_t* lengths;
  size_t count;
  const char* extra;
  bool space;
  bool hyphen;
  bool smallCaps;
} XpSdfontTexts;
int32_t xp_sdfont_pool_prewarm(XpSdfontPool*, const char* (*getter)(const void*, uint32_t), const void*, uint32_t,
                               uint8_t, bool metadata, bool kern, bool accumulate);
int32_t xp_sdfont_pool_advances(XpSdfontPool*, const XpSdfontTexts*, uint8_t mask);
bool xp_sdfont_pool_lookup(const XpSdfontPool*, uint32_t cp, uint8_t style, uint16_t* outAdvance);
uint16_t xp_sdfont_pool_advance(const XpSdfontPool*, uint32_t cp, uint8_t style);
uint8_t xp_sdfont_pool_resolve(const XpSdfontPool*, uint8_t style);
uint8_t xp_sdfont_pool_mask(const XpSdfontPool*, uint8_t mask);
bool xp_sdfont_pool_coverage(const XpSdfontPool*, uint32_t cp, uint8_t style);
const void* xp_sdfont_pool_miss(XpSdfontPool*, uint32_t cp, uint8_t style);
void xp_sdfont_pool_clear_advances(XpSdfontPool*);
void xp_sdfont_pool_clear_overflow(XpSdfontPool*);
void xp_sdfont_pool_clear_cache(XpSdfontPool*);
void xp_sdfont_pool_free_mini(void* style, const XpSdfontServices*);
void xp_sdfont_pool_free_ligatures(void* style, const XpSdfontServices*);
// Host ABI test: field offsets and sizes of the stable native cache slots.
size_t xp_sdfont_pool_layout(uint32_t field);
// Failure details are written only on a parsed rejection; all other outputs and
// state remain unchanged. NULL detail is silent. Caller supplies style/index.
// args: Version=(actual,expected,0,0); StyleCount/StyleId/DuplicateStyle=(value,0,0,0);
// Counts=(intervals,glyphs,kernLeft,kernRight); IntervalOrder=(first,last,0,0);
// IntervalLayout=(overlap,span,offsetMismatch,offsetOverrun); others all zero.
#ifdef __cplusplus
enum class XpSdfontParseError : uint32_t {
  ShortInput,
  Magic,
  Version,
  StyleCount,
  StyleId,
  DuplicateStyle,
  Counts,
  IntervalOrder,
  IntervalLayout
};
#else
typedef uint32_t XpSdfontParseError;
#endif
typedef struct {
  XpSdfontParseError kind;
  uint32_t args[4];
} XpSdfontParseDiagnostic;
// Detail is writable, aligned, and disjoint from input, state and output.
bool xp_sdfont_header_diagnostic(const uint8_t*, size_t, XpSdfontDirectory*, XpSdfontParseDiagnostic*);
bool xp_sdfont_style_diagnostic(const uint8_t*, size_t, XpSdfontDirectory*, XpSdfontStyle*, XpSdfontParseDiagnostic*);
bool xp_sdfont_interval_diagnostic(const uint8_t*, size_t, uint32_t glyphs, uint32_t index, XpSdfontIntervals*,
                                   void* out, XpSdfontParseDiagnostic*);
bool xp_sdfont_header(const uint8_t*, size_t, XpSdfontDirectory*);
bool xp_sdfont_style(const uint8_t*, size_t, XpSdfontDirectory*, XpSdfontStyle*);
// out points to a writable EpdUnicodeInterval (three uint32 fields).
bool xp_sdfont_interval(const uint8_t*, size_t, uint32_t glyphs, uint32_t index, XpSdfontIntervals*, void* out);
bool xp_sdfont_glyph_valid(const uint8_t*, size_t, bool twoBit, uint32_t bitmapOffset, uint32_t fileSize);
// A class ID outside count is returned as zero. Invalid input leaves cp unchanged.
uint8_t xp_sdfont_kern_class(const uint8_t*, size_t, uint8_t count, uint32_t* cp);
bool xp_sdfont_ligature(const uint8_t*, size_t, uint32_t* left, uint32_t* right, uint32_t* out);
bool xp_sdfont_filename(const uint8_t*, size_t, uint8_t* size);

// XTC byte parsing. No allocation, I/O, retained pointers, or UTF-8 normalization.
// Input: len readable bytes in one allocation, len <= PTRDIFF_MAX; NULL only at
// zero length. Output objects are writable and disjoint from inputs and each
// other; natural alignment is required except for the packed header.
// Status matches XtcError: 0 OK, 2 magic, 3 version, 4 corrupt, 6 short/invalid
// input. Outputs remain unchanged on error. Flags/dimensions retain legacy rules.
#pragma pack(push, 1)
typedef struct {
  uint32_t magic;
  uint8_t versionMajor, versionMinor;
  uint16_t pageCount;
  uint8_t readDirection, hasMetadata, hasThumbnails, hasChapters;
  uint32_t currentPage;
  uint64_t metadataOffset, pageTableOffset, dataOffset, thumbOffset;
  uint32_t chapterOffset, padding;
} XpXtcHeader;
#pragma pack(pop)
typedef struct {
  uint64_t offset;
  uint32_t size;
  uint16_t width, height;
} XpXtcEntry;
#ifdef __cplusplus
enum class XpXtcChapterAction : uint16_t { Skip, Emit, Stop };
#else
typedef uint16_t XpXtcChapterAction;
#endif
typedef struct {
  uint16_t name_len, start, end;
  XpXtcChapterAction action;
} XpXtcChapter;
int32_t xp_xtc_header(const uint8_t* data, size_t len, XpXtcHeader* out);
uint8_t xp_xtc_bit_depth(const uint8_t* data, size_t len);  // 0 invalid, 1 XTC, 2 XTCH.
bool xp_xtc_table_valid(const XpXtcHeader* header, uint64_t file_size);
bool xp_xtc_has_chapters(const XpXtcHeader* header);
bool xp_xtc_chapter_flag(uint8_t flag);
int32_t xp_xtc_entry(const uint8_t* data, size_t len, XpXtcEntry* out);
uint32_t xp_xtc_bitmap_size(uint16_t width, uint16_t height, uint8_t depth);
int32_t xp_xtc_page(const uint8_t* data, size_t len, uint8_t depth, bool streaming, uint32_t* size);
uint32_t xp_xtc_metadata_offset(bool author);
size_t xp_xtc_text_len(const uint8_t* data, size_t len);
void xp_xtc_chapter_span(const XpXtcHeader* header, const uint8_t* data, size_t len, uint64_t file_size,
                         uint64_t* offset, uint64_t* count);
int32_t xp_xtc_chapter(const uint8_t* data, size_t len, uint16_t page_count, XpXtcChapter* out);

// Page records (section v68): no allocation; arena bytes are relocatable and
// require no alignment. Status: 0 OK, 1 short input, 2 short output, 3 invalid.
// Input/output allocations and scalar output objects must be live and disjoint.
// Scalar objects/typed input arrays must have their natural alignment. Null
// buffers are allowed only at zero length; lengths must fit PTRDIFF_MAX.
// Query only successfully decoded arenas, unchanged for the lifetime of spans.
typedef struct {
  const uint8_t* data;
  size_t len;
} XpPageSpan;
typedef struct {
  size_t arena_size, consumed;
  uint16_t elements, footnotes, links;
} XpPageInfo;
typedef struct {
  uint32_t tag;
  XpSecRecord record;
  XpPageSpan first, second;
  int16_t width, height;
} XpPageElement;
typedef struct {
  XpSecRecord block;
  int32_t fonts[3];
  uint16_t count, paragraph_start;
  uint8_t has_fonts, has_ruby, has_focus;
  XpPageSpan packed, font_data;  // Multi-byte packed values may be unaligned.
  // Little-endian u32 arena offsets to [u32 length, bytes, NUL] ruby records.
  // Empty when has_ruby is false; otherwise one offset per word.
  XpPageSpan ruby_offsets;
  XpPageSpan style_data;
} XpPageLine;
typedef struct {
  XpPageSpan text, ruby;
  uint32_t source_start, source_end;
  int32_t font;
  int16_t x;
  uint16_t focus_suffix;
  uint8_t style, focus_boundary, starts_paragraph, discretionary_hyphen, small_caps;
} XpPageWord;
typedef struct {
  XpPageSpan href;
  int16_t x, y, width, height;
} XpPageLink;
typedef struct {
  uintptr_t words[8];
} XpPageBuilder;
// Line-only page capacity, with no footnotes/links. text_bytes includes word NULs;
// ruby_bytes excludes length prefixes. Nonzero focus/fonts allows that metadata
// on every line. Counts may be upper bounds. Both sizing calls return SIZE_MAX on overflow.
size_t xp_page_line_capacity(size_t lines, size_t words, size_t text_bytes, uint8_t focus, uint8_t fonts,
                             size_t ruby_bytes);
// Decoded arena bound for line-only pages without ruby annotations.
size_t xp_page_plain_line_arena_capacity(size_t encoded_bytes, size_t lines);
// Parses one page prefix, matching Page::deserialize; consumed identifies its end.
// Decode reports required arena_size even on short output and leaves it untouched.
// Probe performs bounded reads for sizing only; decode validates the final bytes.
// Failure-only details; no callbacks, allocations, or borrowed pointers.
// a/b: WORD_COUNT=count/0, TEXT_SIZE=bytes/words, RULE=width/thickness;
// BORDER_STYLE, TAG, counts, FONT_FLAG, FONT_SLOT=value/0;
// ARENA_READ=bytes/0; WORD_OFFSET, FOOTNOTE_READ, LINK_READ, LINK_GEOMETRY=index/0.
// Other events use zero/zero. element=1 adds the parent null TextBlock
// message after the detail. Zero means no parent message. Success clears all fields.
// diagnostic may be null; otherwise it must be aligned, writable and disjoint.
typedef enum {
  XP_PAGE_DIAG_NONE = 0,
  XP_PAGE_DIAG_WORD_COUNT = 1,
  XP_PAGE_DIAG_TEXT_SIZE = 2,
  XP_PAGE_DIAG_RULE = 3,
  XP_PAGE_DIAG_DROP_CAP = 4,
  XP_PAGE_DIAG_BORDER_STYLE = 5,
  XP_PAGE_DIAG_EMPTY_BORDER = 6,
  XP_PAGE_DIAG_TAG = 7,
  XP_PAGE_DIAG_FOOTNOTE_COUNT = 8,
  XP_PAGE_DIAG_LINK_COUNT = 9,
  XP_PAGE_DIAG_FONT_FLAG = 10,
  XP_PAGE_DIAG_ARENA_READ = 11,
  XP_PAGE_DIAG_ARENA_LAYOUT = 12,
  XP_PAGE_DIAG_WORD_OFFSET = 13,
  XP_PAGE_DIAG_WORD_FONTS = 14,
  XP_PAGE_DIAG_FONT_SLOT = 15,
  XP_PAGE_DIAG_FOOTNOTE_READ = 16,
  XP_PAGE_DIAG_LINK_READ = 17,
  XP_PAGE_DIAG_LINK_GEOMETRY = 18,
} XpPageDiagnosticEvent;
typedef struct {
  uint32_t event, a, b, element;
} XpPageDiagnostic;
uint32_t xp_page_probe_diagnostic(void*, uint8_t (*read)(void*, size_t, uint8_t*, size_t), size_t, XpPageInfo*,
                                  XpPageDiagnostic*);
uint32_t xp_page_arena_size_diagnostic(const uint8_t*, size_t, XpPageInfo*, XpPageDiagnostic*);
uint32_t xp_page_decode_diagnostic(const uint8_t*, size_t, uint32_t, uint8_t*, size_t, XpPageInfo*, XpPageDiagnostic*);
uint32_t xp_page_decode_in_place_diagnostic(uint8_t*, size_t, size_t, size_t, uint32_t, XpPageInfo*, XpPageDiagnostic*);
uint32_t xp_page_probe(void* context, uint8_t (*read)(void*, size_t, uint8_t*, size_t), size_t len, XpPageInfo* info);
uint32_t xp_page_decode_in_place(uint8_t* arena, size_t capacity, size_t offset, size_t len, uint32_t visible,
                                 XpPageInfo* info);
uint32_t xp_page_arena_size(const uint8_t*, size_t, XpPageInfo*);
uint32_t xp_page_decode(const uint8_t*, size_t, uint32_t visible_offset, uint8_t*, size_t, XpPageInfo*);
uint32_t xp_page_encode(const uint8_t*, size_t, uint8_t*, size_t, size_t* required);
uint32_t xp_page_info(const uint8_t*, size_t, XpPageInfo*, uint32_t* visible_offset);
// Element record uses XP_SEC_POSITION/RULE/DROP_CAP/BORDER. Image spans are
// cache/source paths with a trailing NUL; drop-cap first is its text (not NUL-terminated).
uint32_t xp_page_element(const uint8_t*, size_t, uint16_t, XpPageElement*);
uint32_t xp_page_line(const uint8_t*, size_t, uint16_t, XpPageLine*);
typedef struct {
  int16_t x, y;
  uint16_t tag;
} XpPagePosition;
// Tag/position without decoding the element payload; invalid input has tag zero.
XpPagePosition xp_page_position(const uint8_t*, size_t, uint16_t);
// Word text.len excludes its trailing NUL. Ruby records on has_ruby lines also
// have a trailing NUL; absent ruby is an empty span.
uint32_t xp_page_word(const uint8_t*, size_t, uint16_t element, uint16_t word, int32_t section_font, XpPageWord*);
uint32_t xp_page_footnote(const uint8_t*, size_t, uint16_t, XpPageSpan* number, XpPageSpan* href);
uint32_t xp_page_link(const uint8_t*, size_t, uint16_t, XpPageLink*);
uint32_t xp_page_image_bounds(const uint8_t*, size_t, int16_t bounds[4], uint8_t* found);
// Builder exclusively borrows output until finish/reset. Appends copy inputs
// synchronously and are atomic on failure. Order: elements, footnotes, links.
// No destroy is needed; state owns no allocation. Finish can be retried after
// short output only by rebuilding into a larger caller buffer.
uint32_t xp_page_builder_init(XpPageBuilder*, uint8_t*, size_t);
// XpParaLine arrays remain borrowed for this call. Its layout-only link spans
// remain with the assembly owner, which resolves them to page link rectangles.
// A null line->ruby denotes no annotations; otherwise it has line->count spans.
uint32_t xp_page_builder_line(XpPageBuilder*, int16_t x, int16_t y, const XpParaLine*);
// record kind IMAGE_SIZE/RULE/DROP_CAP/BORDER; x/y override position fields.
uint32_t xp_page_builder_element(XpPageBuilder*, int16_t x, int16_t y, const XpSecRecord*, XpPageSpan, XpPageSpan);
uint32_t xp_page_builder_footnote(XpPageBuilder*, const uint8_t number[32], const uint8_t href[256]);
uint32_t xp_page_builder_link(XpPageBuilder*, const XpPageLink*);
uint32_t xp_page_builder_finish(XpPageBuilder*, size_t* written);

// Streaming inflate: caller-owned state and output history.
size_t xp_inflate_state_size(void);
size_t xp_inflate_state_align(void);
int32_t xp_inflate_init(void* state, size_t state_bytes, uint8_t zlib_wrapped);
// Result: 0 needs input, 1 done, 2 needs output; -1 corrupt, -2 checksum,
// -3 invalid argument, -4 truncated. out_pos advances by produced bytes.
// Buffers and counters must be disjoint. Output must be initialized, including
// unused capacity. Preserve output history between calls.
// For wrapping output, out_len is a power of two and out_pos is its cursor;
// reset out_pos to zero only after it reaches out_len. Non-wrapping output
// retains the same base address and all previous bytes. Lengths <= PTRDIFF_MAX.
int32_t xp_inflate_step(void* state, const uint8_t* input, size_t in_len, uint8_t more_input, uint8_t* output,
                        size_t out_len, size_t* out_pos, uint8_t wrapping_window, size_t* consumed, size_t* produced);

// PNG: synchronous decode; read(offset) writes at most len bytes and returns its count.
// Callbacks/context remain live, do not reenter, and retain no byte spans.
// Target lends exclusive framebuffer storage; write callbacks preserve its loan.
// write operations: 0 open cache, 1 bytes, 2 close cache, 3 abort cache.
// BMP only uses operation 1; target may be null for BMP.
typedef size_t (*XpPngRead)(void*, uint32_t offset, uint8_t*, size_t len);
/* PNG diagnostic protocol: optional synchronous callback, no retained pointers and
 * no reentry to Rust or I/O on borrowed handles. Unlisted a-d arguments are zero.
 * Image.c packs depth in bits 0..7, color type in 8..15, interlace in 16..23.
 * Read-related errors precede subsequent I/O. Events follow call order: BMP Image
 * precedes ordered validation (Method, Interlace, Dimensions, Color, RowSize), then
 * optional Scaling and allocations. Milestones are not proof of overall success.
 * Framebuffer DecodeStart starts a debug-only clock interval; DecodeFailed or
 * DecodeComplete ends it. Specific errors may precede a summary; null callbacks
 * suppress all events without changing decoding, allocation, or I/O behavior.
 */
typedef enum {
  XP_PNG_DIAG_Signature = 1,        // BMP signature check failed.
  XP_PNG_DIAG_Ihdr = 2,             // BMP IHDR check failed.
  XP_PNG_DIAG_Image = 3,            // BMP header read; a/b = canvas pixels, c = packed depth/color/interlace.
  XP_PNG_DIAG_Method = 4,           // BMP rejected compression/filter method.
  XP_PNG_DIAG_Interlace = 5,        // BMP rejected interlace method.
  XP_PNG_DIAG_Dimensions = 6,       // BMP rejected canvas size; a/b = width/height pixels.
  XP_PNG_DIAG_Color = 7,            // BMP rejected color type; a = PNG color-type byte.
  XP_PNG_DIAG_RowSize = 8,          // BMP rejected row length; a = row bytes (low 32 bits).
  XP_PNG_DIAG_Scanline = 9,         // BMP scanline allocation failed; a = pixel row bytes excluding filter byte.
  XP_PNG_DIAG_Idat = 10,            // BMP has no IDAT input.
  XP_PNG_DIAG_Inflate = 11,         // BMP inflate window/workspace allocation failed.
  XP_PNG_DIAG_Scaling = 12,         // BMP scaling chosen before buffer allocation; a/b = source, c/d = output pixels.
  XP_PNG_DIAG_RowScratch = 13,      // BMP output/gray row allocation failed; a = requested bytes.
  XP_PNG_DIAG_Atkinson1 = 14,       // BMP 1-bit dither allocation failed.
  XP_PNG_DIAG_Atkinson2 = 15,       // BMP 2-bit dither allocation failed.
  XP_PNG_DIAG_Accumulators = 16,    // BMP scaling-accumulator allocation failed.
  XP_PNG_DIAG_ScanlineDecode = 17,  // BMP row decode failed; a = zero-based row index.
  XP_PNG_DIAG_Filter = 18,          // BMP unknown filter; a = filter byte; followed by ScanlineDecode.
  XP_PNG_DIAG_Complete = 19,        // BMP output finished successfully, after all writes.
  XP_PNG_DIAG_Open = 20,            // Framebuffer header inspection failed.
  XP_PNG_DIAG_DecoderAlloc = 21,    // Framebuffer decoder allocation failed; a = row bytes for rows, else 0.
  XP_PNG_DIAG_Pipeline = 22,        // Reserved legacy rejection event; now XP_IMAGE_DIAG_Configuration.
  XP_PNG_DIAG_PipelineAlloc = 23,   // Reserved legacy row-allocation event; now XP_IMAGE_DIAG_RowAllocation.
  XP_PNG_DIAG_Cache = 24,           // Framebuffer cache start failed; decode continues without cache.
  XP_PNG_DIAG_DecodeStart = 25,     // Framebuffer timing start, after decoder/pipeline/cache setup, before row decode.
  XP_PNG_DIAG_DecodeFailed = 26,    // Framebuffer format/row decode failed; may follow specific error.
  XP_PNG_DIAG_DecodeComplete = 27,  // Framebuffer row decode succeeded, before pipeline/cache finish; closes timing.
  XP_PNG_DIAG_UnsupportedDepth =
      28,  // Framebuffer rejected depth/color combination; a = bits/sample, b = PNG color type.
  XP_PNG_DIAG_RowBuffer =
      29,  // Framebuffer row limit exceeded; a = required bytes, b = width pixels, c = color type, d = bits/sample.
  XP_PNG_DIAG_FrameDimensions =
      30,  // Framebuffer rejected canvas size; a/b = width/height pixels (int32 bits for signed logs).
  XP_PNG_DIAG_GrayWidth = 31,    // Framebuffer expanded gray width rejected; a = width pixels (= expanded row bytes).
  XP_PNG_DIAG_FrameFormat = 32,  // a: color type; b: bit depth; precedes XP_IMAGE_DIAG_PngSummary.
} XpPngDiagnostic;
typedef struct {
  void* context;
  XpPngRead read;
  void (*target)(void*, XpImageTarget*);
  bool (*write)(void*, uint32_t operation, const uint8_t*, size_t);
  void (*progress)(void*);
  uint32_t size;
  // Optional; synchronous and non-reentrant. No retained pointers.
  void (*diagnostic)(void*, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
} XpPngServices;
typedef struct {
  int32_t width, height;
  bool oneBit, crop, originalThresholds;
} XpPngBmpConfig;
bool xp_png_dimensions(void*, XpPngRead, uint32_t size, uint32_t* width, uint32_t* height);
bool xp_png_render(const XpPngServices*, const XpImageConfig*, bool cache);
// Optional exclusive framebuffer scratch loan, aligned to sizeof(uintptr_t).
size_t xp_png_scratch_size(void);
bool xp_png_bmp(const XpPngServices*, const XpPngBmpConfig*, uint8_t* scratch, size_t scratch_len);
bool xp_png_sleep_header(const uint8_t*, size_t);
// Small canonical-table mode. Same ownership and status contract as xp_inflate_*.
// The output limit bounds one call without shrinking the caller's history ring.
size_t xp_inflate_small_state_size(void);
size_t xp_inflate_small_state_align(void);
int32_t xp_inflate_small_init(void* state, size_t state_bytes, uint8_t zlib_wrapped);
int32_t xp_inflate_small_step(void* state, const uint8_t* input, size_t in_len, uint8_t more_input, uint8_t* output,
                              size_t out_len, size_t* out_pos, uint8_t wrapping_window, size_t* consumed,
                              size_t* produced, size_t out_max);

// Library CLX1. All descriptors and buffers are borrowed for the call only.
// Build status preserves the terminal summary in C++ before dirty-marker cleanup.
enum {
  XP_LIBRARY_BUILD_FAILED = 0,
  XP_LIBRARY_BUILD_UNCHANGED = 1,
  XP_LIBRARY_BUILD_EMIT_FAILED = 2,
  XP_LIBRARY_BUILD_INSTALLED = 3
};
// file operations: 0 read-open, 1 write-open, 2 open, 3 next-entry, 4 close,
// 5 read, 6 write, 7 seek, 8 position, 9 size, 10 mtime, 11 is-directory,
// 12 rewind-directory, 13 get-name. Handles are nonzero, owned until close.
// storage operations: 0 exists, 1 remove, 2 mkdir, 3 rename.
// Allocation returns zeroed max_align_t-aligned storage; release pairs with it.
// Metadata: 0 EPUB, 1 XTC, 2 release. Title is exclusively writable; author is
// readable until release. Release takes null path/output pointers and frees both.
// clock: 0 millis, 1 delay(1). dirty: 0 query, 1 mark, 2 clear.
// Diagnostics: text is a borrowed NUL-terminated path; null callback is silent.
// Events 1..13: recovery/install/dirty cleanup; 14 no metadata (text),
// 15 directory resume (text,a=offset), 16 dedup cap (a=count,text), 17 folder write,
// 18 arrival allocation, 19 stage read (a=offset), 20 folder truncation (a=got,b=want),
// 21 folder open, 22 folder copy, 23 author allocation (a=bytes), 24 canonical OOM,
// 25 spelling OOM, 26 mtime OOM (a=bytes), 27 degraded sort (a=count), 28 stage I/O,
// 29 staged OOM, 30 incomplete emit (a=I/O failed,b=closed,c=written,d=expected),
// 32..33 dirty creation, 34..35 buffer OOM (a=bytes), 36 dedup OOM, 37 prior OOM,
// 38 prior read (a=ordinal), 39 prior open, 40 staging open, 41..42 phase ms (a),
// 43 staging failed, 44 unchanged (a=reused,b=parsed,c=ms), 45 firstSeen OOM,
// 46 reconciliation open, 47 read (a=ordinal), 48 close, 49 reconciliation ms (a),
// 50 order OOM (a=count), 51 title allocation (a=bytes), 52 title stage open,
// 53 title stage read (a=offset), 54 close, 55 sort OOM, 56..57 phase ms (a),
// 59 restored backup over invalid live index; 60 rejected (a=ClixValidity). C++ owns text, levels, and heap log values.
// Callbacks must not unwind, retain borrowed pointers, or reenter Rust.
typedef struct {
  void* context;
  uint8_t (*read)(void*, uint32_t, uint8_t*, size_t);
  uint8_t (*string)(void*, uint32_t, size_t, uint8_t, uint8_t*);
  void (*append)(void*, const uint8_t*, size_t);
  void (*fail)(void*);
} XpLibraryIndexIo;
typedef struct {
  uint64_t hash;
  uint32_t size;
} XpLibraryIdentity;
typedef struct {
  uint16_t books, folders, duplicates, unreadable;
  uint32_t walk_ms;
  uint16_t unchanged, added, renamed, removed, enriched, parsed, reused;
  uint8_t replaced, ranks_degraded, dedup_degraded;
} XpLibraryStats;
typedef struct {
  void* context;
  int64_t (*file)(void*, uint8_t, uint32_t, const uint8_t*, uint8_t*, size_t, uint64_t);
  uint8_t (*storage)(void*, uint8_t, const uint8_t*, const uint8_t*);
  uint8_t* (*allocate)(void*, size_t);
  void (*release)(void*, uint8_t*);
  uint32_t (*clock)(void*, uint8_t);
  uint8_t (*metadata)(void*, const uint8_t*, uint8_t, uint8_t**, size_t*, const uint8_t**, size_t*);
  void (*diagnostic)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint8_t*);
} XpLibraryServices;
size_t xp_library_text(uint8_t*, size_t, uint8_t);
uint8_t xp_library_matches(const uint8_t*, size_t, const uint8_t*, size_t);
uint32_t xp_library_initial(const uint8_t*, size_t);
uint64_t xp_library_hash(const uint8_t*, size_t);
uint32_t xp_library_align(uint32_t);
void xp_library_layout(void*, uint32_t, uint32_t);
uint8_t xp_library_validate(const void*, uint64_t, uint8_t);
uint32_t xp_library_offset(const void*, uint16_t, uint8_t);
uint32_t xp_library_read(const void*, XpLibraryIndexIo*, void*, uint8_t, uint32_t, uint64_t*);
uint8_t xp_library_recent(const void*, XpLibraryIndexIo*, const XpLibraryIdentity*, size_t, uint16_t*, uint8_t*);
size_t xp_library_join(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*);
uint8_t xp_library_book(const uint8_t*, size_t);
uint8_t xp_library_build(const XpLibraryServices*, const uint8_t*, size_t, XpLibraryStats*, uint8_t, uint8_t*);
uint8_t xp_library_dirty(const XpLibraryServices*, uint8_t*, uint8_t);

typedef enum {
  XP_SYNC_QUERY_BOOK_SIZE = 0,
  XP_SYNC_QUERY_SPINE_COUNT = 1,
  XP_SYNC_QUERY_SPINE_FOR_SIZE = 2,
  XP_SYNC_QUERY_CUMULATIVE_SIZE = 3,
  XP_SYNC_QUERY_CACHED_PAGES = 4,
  XP_SYNC_QUERY_PAGE_FOR_OFFSET = 5,
  XP_SYNC_QUERY_PAGE_FOR_PARAGRAPH = 6,
  XP_SYNC_QUERY_PAGE_FOR_LIST = 7,
  XP_SYNC_QUERY_PAGE_FOR_ANCHOR = 8,
  XP_SYNC_QUERY_ITEM_SIZE = 9
} XpSyncQuery;
typedef enum {
  XP_SYNC_READ_CSTRING = 0,
  XP_SYNC_READ_STRING = 1,
  XP_SYNC_READ_U8 = 2,
  XP_SYNC_READ_U8_DEFAULT = 3,
  XP_SYNC_READ_BOOL = 4,
  XP_SYNC_READ_F32 = 5,
  XP_SYNC_READ_I64 = 6,
  XP_SYNC_READ_U32 = 7,
  XP_SYNC_READ_U16 = 8,
  XP_SYNC_READ_NULL = 9,
  XP_SYNC_READ_OBJECT = 10,
  XP_SYNC_READ_RAW = 11
} XpSyncJsonRead;
typedef enum {
  XP_SYNC_VALUE_STRING = 0,
  XP_SYNC_VALUE_U8 = 1,
  XP_SYNC_VALUE_BOOL = 2,
  XP_SYNC_VALUE_F32 = 3,
  XP_SYNC_VALUE_I64 = 4,
  XP_SYNC_VALUE_U32 = 5,
  XP_SYNC_VALUE_U16 = 6,
  XP_SYNC_VALUE_SECRET = 7,
  XP_SYNC_VALUE_RAW = 8
} XpSyncValueKind;
typedef enum {
  XP_SYNC_STORE_USERNAME = 0,
  XP_SYNC_STORE_PASSWORD = 1,
  XP_SYNC_STORE_SERVER_URL = 2,
  XP_SYNC_STORE_SERVER_TYPE = 3,
  XP_SYNC_STORE_MATCH_METHOD = 4,
  XP_SYNC_STORE_SEND_METADATA = 5,
  XP_SYNC_STORE_SYNC_CLIPPINGS = 6,
  XP_SYNC_STORE_BEHAVIOR = 7,
  XP_SYNC_STORE_HEADER0_NAME = 8,
  XP_SYNC_STORE_HEADER0_VALUE = 9,
  XP_SYNC_STORE_HEADER1_NAME = 10,
  XP_SYNC_STORE_HEADER1_VALUE = 11,
  XP_SYNC_STORE_EXTRACTED_PASSWORD = 12,
  XP_SYNC_STORE_BASE_URL = 13
} XpSyncStoreField;
typedef enum {
  XP_SYNC_HTTP_STATUS = 0,
  XP_SYNC_HTTP_URL_RESET = 1,
  XP_SYNC_HTTP_URL_APPEND = 2,
  XP_SYNC_HTTP_HEAP = 3,
  XP_SYNC_HTTP_INSECURE = 4,
  XP_SYNC_HTTP_BEGIN = 5,
  XP_SYNC_HTTP_HEADER = 6,
  XP_SYNC_HTTP_GET = 7,
  XP_SYNC_HTTP_POST = 8,
  XP_SYNC_HTTP_PUT = 9,
  XP_SYNC_HTTP_END = 10,
  XP_SYNC_HTTP_JSON_RESET = 11,
  XP_SYNC_HTTP_PARSE_RESPONSE = 12,
  XP_SYNC_HTTP_PARSE_EXTRA = 13,
  XP_SYNC_HTTP_SERIALIZE = 14,
  XP_SYNC_HTTP_RELEASE_EXTRA = 15
} XpSyncTransportOp;
typedef enum {
  XP_SYNC_METHOD_AUTH = 0,
  XP_SYNC_METHOD_CREATE = 1,
  XP_SYNC_METHOD_GET = 2,
  XP_SYNC_METHOD_UPDATE = 3
} XpSyncMethod;
typedef enum {
  XP_SYNC_HTTP_DIAG_NO_CREDENTIALS = 0,
  XP_SYNC_HTTP_DIAG_AUTH = 1,
  XP_SYNC_HTTP_DIAG_CREATE = 2,
  XP_SYNC_HTTP_DIAG_GET = 3,
  XP_SYNC_HTTP_DIAG_UPDATE = 4,
  XP_SYNC_HTTP_DIAG_LOW_HEAP = 5,
  XP_SYNC_HTTP_DIAG_BAD_URL = 6,
  XP_SYNC_HTTP_DIAG_AUTH_RESPONSE = 7,
  XP_SYNC_HTTP_DIAG_CREATE_RESPONSE = 8,
  XP_SYNC_HTTP_DIAG_GET_RESPONSE = 9,
  XP_SYNC_HTTP_DIAG_UNUSED = 10,
  XP_SYNC_HTTP_DIAG_JSON_ERROR = 11,
  XP_SYNC_HTTP_DIAG_RICH_POSITION = 12,
  XP_SYNC_HTTP_DIAG_PROGRESS = 13,
  XP_SYNC_HTTP_DIAG_REQUEST_BODY = 14,
  XP_SYNC_HTTP_DIAG_UPDATE_RESPONSE = 15
} XpSyncHttpDiagnostic;
typedef enum {
  XP_SYNC_MAP_DIAG_UNUSED = 0,
  XP_SYNC_MAP_DIAG_ANCESTRY = 1,
  XP_SYNC_MAP_DIAG_BODY_TEXT = 2,
  XP_SYNC_MAP_DIAG_PARAGRAPH = 3,
  XP_SYNC_MAP_DIAG_CHAPTER_START = 4,
  XP_SYNC_MAP_DIAG_CONTENT_OFFSET = 5,
  XP_SYNC_MAP_DIAG_NO_OFFSET_LUT = 6,
  XP_SYNC_MAP_DIAG_PERCENTAGE = 7,
  XP_SYNC_MAP_DIAG_LIST_PAGE = 8,
  XP_SYNC_MAP_DIAG_NO_LIST_PAGE = 9,
  XP_SYNC_MAP_DIAG_ANCHOR_PAGE = 10,
  XP_SYNC_MAP_DIAG_NO_ANCHOR_PAGE = 11,
  XP_SYNC_MAP_DIAG_PARAGRAPH_PAGE = 12,
  XP_SYNC_MAP_DIAG_NO_PARAGRAPH_PAGE = 13,
  XP_SYNC_MAP_DIAG_RICH_OUT_OF_RANGE = 14,
  XP_SYNC_MAP_DIAG_RICH_NO_CACHE = 15,
  XP_SYNC_MAP_DIAG_RICH_EXACT = 16,
  XP_SYNC_MAP_DIAG_RICH_PARAGRAPH = 17,
  XP_SYNC_MAP_DIAG_RICH_SCALED = 18
} XpSyncMapDiagnostic;
typedef enum {
  XP_SYNC_STORE_DIAG_UNUSED = 0,
  XP_SYNC_STORE_DIAG_PIN_LEGACY = 1,
  XP_SYNC_STORE_DIAG_BAD_SERVER_TYPE = 2,
  XP_SYNC_STORE_DIAG_BAD_MATCH_METHOD = 3,
  XP_SYNC_STORE_DIAG_BAD_BEHAVIOR = 4
} XpSyncStoreDiagnostic;
typedef enum {
  XP_SYNC_PROTOCOL_USERNAME = 0,
  XP_SYNC_PROTOCOL_PASSWORD = 1,
  XP_SYNC_PROTOCOL_MD5 = 2,
  XP_SYNC_PROTOCOL_BASIC = 3,
  XP_SYNC_PROTOCOL_HEADER0_NAME = 4,
  XP_SYNC_PROTOCOL_HEADER0_VALUE = 5,
  XP_SYNC_PROTOCOL_HEADER1_NAME = 6,
  XP_SYNC_PROTOCOL_HEADER1_VALUE = 7,
  XP_SYNC_PROTOCOL_SERVER_TYPE = 8,
  XP_SYNC_PROTOCOL_DOCUMENT = 100,
  XP_SYNC_PROTOCOL_PROGRESS = 101,
  XP_SYNC_PROTOCOL_PERCENTAGE = 102,
  XP_SYNC_PROTOCOL_DEVICE = 103,
  XP_SYNC_PROTOCOL_DEVICE_ID = 104,
  XP_SYNC_PROTOCOL_TIMESTAMP = 105,
  XP_SYNC_PROTOCOL_HAS_METADATA = 110,
  XP_SYNC_PROTOCOL_FILENAME = 111,
  XP_SYNC_PROTOCOL_TITLE = 112,
  XP_SYNC_PROTOCOL_AUTHORS = 113,
  XP_SYNC_PROTOCOL_EXTRA_JSON = 114,
  XP_SYNC_PROTOCOL_ISBN = 115,
  XP_SYNC_PROTOCOL_ASIN = 116,
  XP_SYNC_PROTOCOL_SERIES = 117,
  XP_SYNC_PROTOCOL_SERIES_INDEX = 118,
  XP_SYNC_PROTOCOL_HAS_POSITION = 120,
  XP_SYNC_PROTOCOL_PCT_Q = 121,
  XP_SYNC_PROTOCOL_SPINE = 122,
  XP_SYNC_PROTOCOL_PAGE = 123,
  XP_SYNC_PROTOCOL_PAGES = 124,
  XP_SYNC_PROTOCOL_PARAGRAPH = 125,
  XP_SYNC_PROTOCOL_XPATH = 126
} XpSyncProtocolField;
// All spans and service contexts are borrowed for the synchronous call only.
// Services must not retain spans. Diagnostic callbacks must not reenter Rust.
typedef struct {
  const uint8_t* data;
  size_t len;
} XpSyncSpan;
typedef struct {
  int32_t spine, page, pages;
  uint32_t offset;
  uint8_t has_offset;
  uint16_t paragraph;
  uint8_t has_paragraph;
  uint16_t li;
  uint8_t has_li;
  uint8_t anchor[64];
  uint8_t resolved, mapped;
} XpSyncPosition;
typedef struct {
  uint32_t pct;
  uint16_t spine, page, pages, paragraph;
  uint8_t has_paragraph;
} XpSyncRich;
typedef struct XpSyncReverse XpSyncReverse;
typedef struct XpSyncXPath XpSyncXPath;
typedef struct {
  uint32_t id;
  int32_t a, b, c, d;
  float ratio;
  size_t target, total;
  XpSyncSpan text;
} XpSyncMapEvent;
// Query ids: book size, spine count, spine for size, cumulative size, cached pages,
// offset page, paragraph page, list page, anchor page, item size. Missing values return -1.
typedef struct {
  void* ctx;
  int64_t (*query)(void*, uint32_t, int32_t, size_t, XpSyncSpan);
  uint8_t (*stream)(void*, int32_t, XpSyncReverse*);
  void (*diagnose)(void*, const XpSyncPosition*, const XpSyncMapEvent*);
} XpSyncMapServices;
typedef struct {
  XpSyncMapServices services;
  float (*progress)(void*, int32_t, float);
  uint8_t (*xpath)(void*, int32_t, uint32_t, uint32_t, float);
  void (*append)(void*, XpSyncSpan);
} XpSyncSaveServices;
XpSyncSpan xp_sync_filename(XpSyncSpan);
size_t xp_sync_samples(void*, size_t, size_t (*)(void*, size_t, size_t));
uint8_t xp_sync_compare(const XpSyncPosition*, float, const XpSyncPosition*, float);
void xp_sync_map(const XpSyncMapServices*, XpSyncSpan, float, int32_t, int32_t, int32_t, XpSyncPosition*);
uint8_t xp_sync_rich(const XpSyncMapServices*, const XpSyncRich*, XpSyncSpan, uint8_t, XpSyncPosition*);
float xp_sync_save(const XpSyncSaveServices*, const XpSyncPosition*);
// Only the stream callback may feed its borrowed reverse handle.
void xp_sync_reverse_feed(XpSyncReverse*, XpSyncSpan, int32_t (*)(const uint8_t*, size_t));
// XPath modes: paragraph, exclusive visible offset, inclusive progress offset, count.
int32_t xp_sync_xpath_create(uint32_t, int32_t, size_t, XpSyncXPath**);
uint32_t xp_sync_xpath_feed(XpSyncXPath*, XpSyncSpan, uint8_t);
XpSyncSpan xp_sync_xpath_output(const XpSyncXPath*);
size_t xp_sync_xpath_count(const XpSyncXPath*);
void xp_sync_xpath_destroy(XpSyncXPath*);
size_t xp_sync_xpath_target(size_t, float);
// JSON conversion/serialization leaves. kind is string/u8/bool/f32/i64/u32/u16/secret/raw.
// Secret writes use the existing device-key obfuscation; raw copies a JSON scalar.
typedef struct {
  uint32_t kind;
  int64_t number;
  double real;
  XpSyncSpan text;
  uint8_t valid;
} XpSyncValue;
typedef struct {
  void* ctx;
  XpSyncValue (*get)(void*, uint32_t, const char*, uint32_t);
  void (*set)(void*, uint32_t, const char*, XpSyncValue);
  void (*select)(void*, uint32_t, const char*, size_t, uint32_t, uint32_t);
  size_t (*count)(void*, uint32_t);
  const char* (*key)(void*, uint32_t, size_t);
} XpSyncJson;
typedef struct {
  void* ctx;
  XpSyncValue (*get)(void*, uint32_t);
  void (*set)(void*, uint32_t, XpSyncValue);
} XpSyncModel;
XpSyncSpan xp_sync_base_url(XpSyncSpan, uint8_t*);
uint8_t xp_sync_server_type(uint8_t);
uint8_t xp_sync_behavior(uint8_t);
void xp_sync_store_save(const XpSyncJson*, const XpSyncModel*);
uint8_t xp_sync_store_load(const XpSyncJson*, const XpSyncModel*, uint8_t, void (*)(void*, uint32_t, uint32_t));
typedef struct {
  void* ctx;
  XpSyncValue (*call)(void*, uint32_t, XpSyncSpan, XpSyncValue);
  void (*diagnostic)(void*, uint32_t, int32_t);
} XpSyncTransport;
uint32_t xp_sync_request(uint32_t, const XpSyncJson*, const XpSyncModel*, const XpSyncTransport*);
const char* xp_sync_error_string(uint32_t);

// Assets: synchronous borrowed buffers; callbacks must not reenter asset APIs.
// Buffers are borrowed only for a callback, except the image open-cache path below.
// Asset buffer platform leaves use the display ABI caps convention: 3 = DEFAULT
// (malloc domain, including configured PSRAM preference). Rust chooses sizes and
// lifetimes. Nonzero sizes fit isize; returned storage is aligned for u32/i16.
// Allocation failure returns null. Failed realloc preserves the original owner.
// Free accepts null; every successful allocation/reallocation uses this paired free.
// C3 retains its existing internal allocator path; these leaves serve other targets.
void* xp_assets_platform_alloc(size_t bytes, uint32_t caps);
void* xp_assets_platform_realloc(void* data, size_t bytes, uint32_t caps);
void xp_assets_platform_free(void* data);
// JPEG diagnostic(event, a, b, c, d), stable event numbers:
// 0 dimensions (source width,height); 1 progressive grid (decoded width,height);
// 2 invalid dimensions (source width,height); 3 scaling and 7 smoothing
// (decoded width,height, output width,height); 4 MCU allocation failure
// (decoded width * 16 bytes); 5 row and 6 smoothing allocation failure
// (a is the same legacy MCU byte count, not the failed allocation size);
// 8 scaling, 9 1-bit dither, 10 2-bit dither allocation failure; 11 block origin
// (x,y, decoded width,height); 12 missing rows. Unlisted payload fields are zero.
// Events report observations only; callbacks must not change conversion policy.
typedef struct {
  int32_t width, height;
  bool one_bit, crop, original_thresholds;
} XpAssetJpegConfig;
typedef bool (*XpAssetJpegBlock)(void*, int32_t, int32_t, int32_t, int32_t, int32_t, const uint8_t*);
typedef struct {
  void* context;
  void (*write)(void*, const uint8_t*, size_t);
  void (*progress)(void*);
  void (*diagnostic)(void*, uint32_t, int32_t, int32_t, int32_t, int32_t);
  bool (*decode)(void*, void*, XpAssetJpegBlock);
} XpAssetJpegServices;
bool xp_assets_jpeg_bmp(int32_t, int32_t, bool, const XpAssetJpegConfig*, const XpAssetJpegServices*);
// Screenshot result: 0 success, 1 write/geometry failure, 2 legacy row capacity exceeded.
uint32_t xp_assets_screenshot(const uint8_t*, size_t, uint32_t, uint32_t, void*,
                              bool (*)(void*, const uint8_t*, size_t));
// QR bitmap uses contiguous row-major bits, MSB first, with no row padding.
// plan returns required capacity; bitmap allocates nothing and sets size=0 on failure
// after validating the input/output pointers.
size_t xp_assets_qr_plan(const uint8_t*, size_t, uint8_t*, size_t*);
bool xp_assets_qr_bitmap(const uint8_t*, size_t, uint8_t*, size_t, uint8_t*, void*,
                         bool (*)(void*, const uint8_t*, size_t, uint8_t, uint8_t*));
void xp_assets_qr_draw(const uint8_t*, size_t, uint8_t, int32_t, int32_t, int32_t, int32_t, void*,
                       void (*)(void*, int32_t, int32_t, int32_t));
// Image calls share the existing page-render owner and must be serialized with reset.
// The target stays exclusively writable across IO callbacks until the next target call.
typedef struct {
  int32_t x, y, width, height, screen_width, screen_height;
  bool scanning, intersects;
} XpAssetImageConfig;
// IO: 0 open cache (NUL-terminated path), 1 read data, 2 seek len, 3 size,
// 4 close, 5 free heap, 6 largest block, 7 extract, 8 source size (SIZE_MAX missing),
// 9 decode (NUL-terminated cache path; 0 no decoder, 1 failure, 2 success).
// Image diagnostic(event, a, b), stable event numbers:
// 0 invalid cache; 1 cache loading (width,height); 2 RAM cache loaded;
// 3 row/path allocation failure; 4 cache read failure (row,0); 5 stream complete;
// 6 render; 7 bounds; 8 missing source; 9 empty source; 10 decode;
// 11 missing decoder; 12 decode failure; 13 decode complete.
// All other payloads are zero. Paths, placement and screen geometry come from
// the caller's context. Events do not transfer ownership or request recovery.
// The open-cache path remains valid through the following InvalidCache (0) or
// CacheLoading (1) diagnostic only. Other borrowed pointers must not be retained.
typedef struct {
  void* context;
  size_t (*io)(void*, uint32_t, const uint8_t*, uint8_t*, size_t);
  void (*target)(void*, XpImageTarget*);
  void (*diagnostic)(void*, uint32_t, int32_t, int32_t);
} XpAssetImageCallbacks;
void xp_assets_image_reset(bool release);
bool xp_assets_image_failed(const uint8_t*, size_t);
bool xp_assets_image_valid(const uint8_t*, size_t, int32_t, int32_t, const XpAssetImageCallbacks*);
uint32_t xp_assets_image_render(const uint8_t*, size_t, const XpAssetImageConfig*, const XpAssetImageCallbacks*);
size_t xp_assets_limited_write(size_t*, const uint8_t*, size_t, void*, size_t (*)(void*, const uint8_t*, size_t));

#ifdef __cplusplus
enum class XpFontCacheEvent : uint32_t {
  Decompression = 1,
  MissingGroup = 2,
  HotGroupAllocation = 3,
  ScratchAllocation = 4,
  SlotsFull = 5,
  GlyphCap = 6,
  GroupCap = 7,
  PageAllocation = 8,
  TempAllocation = 9,
  Prewarm = 10
};
#endif
// Built-in cache state is inline in FontDecompressor; no extra owner allocation.
// Fonts and glyph indices are validated resident tables. Callbacks are synchronous,
// cannot reenter the cache, and cannot retain any borrowed pointer.
#ifdef __cplusplus
#define XP_FONT_STAT_ZERO = 0
#else
#define XP_FONT_STAT_ZERO
#endif
typedef struct XpFontCacheStats {
  uint32_t cacheHits XP_FONT_STAT_ZERO;
  uint32_t cacheMisses XP_FONT_STAT_ZERO;
  uint32_t decompressTimeMs XP_FONT_STAT_ZERO;
  uint16_t uniqueGroupsAccessed XP_FONT_STAT_ZERO;
  uint32_t pageBufferBytes XP_FONT_STAT_ZERO;
  uint32_t pageGlyphsBytes XP_FONT_STAT_ZERO;
  uint32_t hotGroupBytes XP_FONT_STAT_ZERO;
  uint32_t peakTempBytes XP_FONT_STAT_ZERO;
  uint32_t getBitmapTimeUs XP_FONT_STAT_ZERO;
  uint32_t getBitmapCalls XP_FONT_STAT_ZERO;
} XpFontCacheStats;
#undef XP_FONT_STAT_ZERO
typedef struct {
  uint32_t glyphIndex, bufferOffset, alignedOffset;
} XpFontCacheEntry;
typedef struct {
  uint8_t* buffer;
  const void* fontData;
  XpFontCacheEntry* glyphs;
  uint16_t glyphCount;
} XpFontCacheSlot;
typedef struct {
  XpFontCacheStats stats;
  XpFontCacheSlot pageSlots[8];
  uint8_t pageSlotCount;
  const void* hotGroupFont;
  uint16_t hotGroupIndex;
  uint8_t* hotGroup;
  uint32_t hotGroupCapacity;
  uint8_t* hotGlyphBuf;
  uint32_t hotGlyphBufCapacity;
} XpFontCache;
typedef struct {
  void* ctx;
  uint8_t* (*alloc)(size_t);
  void (*free)(uint8_t*);
  // -1: initialization failed; 0: decoding failed; 1: exact output produced.
  int32_t (*inflate)(void*, const uint8_t*, uint32_t, uint8_t*, uint32_t);
  uint32_t (*clock)(bool microseconds);
  void (*generation)();
  void (*diagnostic)(void*, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
} XpFontCacheServices;
void xp_font_cache_clear(XpFontCache*, const XpFontCacheServices*);
const uint8_t* xp_font_cache_bitmap(XpFontCache*, const void* font, const void* glyph, uint32_t index,
                                    const XpFontCacheServices*);
int32_t xp_font_cache_prewarm(XpFontCache*, const void* font, const uint8_t* text, size_t length,
                              const XpFontCacheServices*);
// Loading borrows a live HAL file and four style slots. Shared intervals have
// sixteen process-wide slots, serialized by the existing font owner. Allocated
// style/table storage remains facade-owned and is released through its allocator.
#ifdef __cplusplus
enum class XpSdfontLoadEvent : uint32_t {
  HeaderRead = 1,
  StyleAllocation = 2,
  TocRead = 3,
  IntervalSeek = 4,
  IntervalRead = 5,
  Shared = 6,
  IntervalRewind = 7,
  CompactAllocation = 8,
  CompactRead = 9,
  IntervalAllocation = 10,
  IntervalsRead = 11,
  KernIndexAllocation = 12
};
#endif
// SD loader callbacks borrow the current HAL file. Style records and the
// sixteen shared-interval entries retain their existing C++ storage and size.
typedef struct {
  const void* table;
  uint32_t count;
  bool bmp16;
  uint16_t refs;
} XpSdfontShared;
typedef struct {
  void* ctx;
  int32_t (*read)(void*, uint32_t offset, uint8_t* dst, size_t length, bool seek);
  void* (*allocStyles)(void*, uint8_t count);
  uint8_t* (*alloc)(size_t);
  void (*cleanup)(void*);
  void (*setup)(void*, uint8_t);
  void (*diagnostic)(void*, uint32_t, uint32_t, uint32_t, uint32_t);
  void (*parseError)(const XpSdfontParseDiagnostic*, uint32_t, uint32_t);
} XpSdfontLoadServices;
bool xp_sdfont_load(uint8_t* slots, uint32_t* hash, XpSdfontShared*, const XpSdfontLoadServices*);
void xp_sdfont_clear_loaded(XpSdfontPool*, XpSdfontShared*, uint8_t count, bool all, void* ctx,
                            void (*setup)(void*, uint8_t), void (*generation)());
// Font tables are borrowed for this call. Miss callbacks can replace resident
// tables; returned glyph pointers expire at the owner's next cache mutation.
const void* xp_font_lookup(const void*, uint32_t);
bool xp_font_coverage(const void*, uint32_t);
int8_t xp_font_dynamic_kern(const void*, uint32_t, uint32_t);
const void* xp_font_metrics(const void*, uint32_t, XpFontGlyph*);
uint8_t xp_font_style(uint8_t, bool, bool, bool);
// Discovery callbacks borrow names for one call. The facade owns directory
// handles and C++ containers; Rust retains no pointers after the operation.
#ifdef __cplusplus
enum class XpFontDiscoveryEvent : uint32_t { MissingRoot = 1, NotDirectory = 2, Duplicate = 3, Family = 4, Vector = 5 };
#endif
typedef struct {
  const char* name;
  uint8_t size, style;
} XpFontFile;
// get returns a plain record for index < count. The owner and borrowed filenames
// must remain live and unchanged throughout the synchronous operation.
typedef struct XpFontFiles {
  const void* ctx;
  size_t count;
  XpFontFile (*get)(const void*, size_t index);
} XpFontFiles;
typedef struct {
  size_t index;
  uint16_t weight;
  bool italic;
} XpFontCandidate;
int32_t xp_font_file_nearest(const XpFontFiles*, uint8_t size, uint8_t style, bool exact);
bool xp_font_file_path(const char* root, const char* family, const char* file, uint8_t size, bool loose, uint8_t* out,
                       size_t capacity);
bool xp_font_select_faces(const XpFontCandidate*, size_t, const XpFontFiles*, int32_t* selected);
typedef struct {
  void* ctx;
  bool vectors;
  int32_t (*open)(void*, const char*, bool root);
  bool (*next)(void*, bool root, uint8_t* name, bool* directory);
  void (*close)(void*, bool root);
  size_t (*families)(void*);
  const char* (*familyName)(void*, size_t);
  void (*beginFamily)(void*, const char* root, const uint8_t* name, size_t length, bool loose);
  XpFontFiles (*files)(void*, bool vector);
  bool (*addFile)(void*, bool vector, const char* name, uint8_t size, uint8_t style, bool canonical);
  size_t (*commitFamily)(void*, bool vector, const char* directory);
  void (*diagnostic)(void*, uint32_t, const char*, const char*, size_t);
  void (*endFamily)(void*);
} XpFontDiscovery;
void xp_font_scan_root(const XpFontDiscovery*, const char* root);
void xp_font_sizes(const XpFontFiles*, void*, void (*emit)(void*, uint8_t));
int32_t xp_font_find_family(const void*, size_t, const char* (*get)(const void*, size_t), const uint8_t*, size_t);
const char* xp_font_root(const char* family, const char* hidden, const char* visible, bool (*exists)(const char*),
                         bool write);

bool xp_font_has_size(const XpFontFiles*, uint8_t);
typedef struct {
  void* ctx;
  void (*clear)(void*);
  void (*scan)(void*, const char*);
  size_t (*sort)(void*);
  void (*resize)(void*, size_t);
  size_t (*finish)(void*);
} XpFontRegistry;
bool xp_font_discover(const XpFontRegistry*, const char* hidden, const char* visible, size_t maximum);
// Face sources are borrowed and must outlive the family. Each service call
// lends existing vector storage; buffer pointers expire after reserve/resize.
// No callback may reenter this face cache; eviction locks its owner against release.
#ifdef __cplusplus
enum class XpVectorHeapMetric : uint8_t { LargestPsram, LargestInternal, FreeInternal };
enum class XpVectorBufferId : uint8_t { Bitmap, Glyphs, Codepoints, Slots, KernKeys, KernValues };
#endif
typedef struct {
  uint32_t pair, ligatureCp;
} XpVectorLigature;
typedef struct {
  size_t used, cap;
  uint16_t maxGlyphs;
  bool twoBit;
  uint16_t sizePx;
  uint8_t srcIndex;
  int32_t weight;
  bool wantItalic, inited, ready;
  uint32_t ligGid[5];
  XpVectorLigature ligPairs[5];
  uint8_t ligPairCount;
} XpVectorState;
typedef struct {
  uint8_t* data;
  size_t length, capacity;
} XpVectorBuffer;
typedef struct {
  const uint8_t* pixels;
  uint16_t width, height;
  int16_t left, yoff;
  int32_t advance, metrics;
  bool haveMetrics;
} XpVectorRaster;
typedef struct {
  bool present, streamed;
  const uint8_t* data;
  uint32_t len;
  unsigned long (*read)(void*, unsigned long, unsigned char*, unsigned long);
  void* ctx;
  unsigned long fileSize;
} XpVectorSource;
typedef struct {
  void* ctx;
  XpVectorState* (*state)(void*);
  void (*init)(void*);
  XpVectorBuffer (*buffer)(void*, uint8_t);
  void (*reserve)(void*, uint8_t, size_t);
  void (*resize)(void*, uint8_t, size_t);
  void (*insert)(void*, uint8_t, size_t, const uint8_t*);
  void (*release)(void*, uint8_t);
  size_t (*heap)(uint8_t);
  void (*evict)(void*);
  uint32_t (*glyphId)(void*, uint32_t);
  void (*raster)(void*, uint32_t, XpVectorRaster*);
  int32_t (*kern)(void*, uint32_t, uint32_t);
  bool (*ftInit)(void*, const XpVectorSource*, uint16_t, int32_t, bool);
  void (*options)(void*);
  void (*gposBudget)(void*, size_t);
  void (*gsubBudget)(void*, size_t);
  uint32_t (*ligature)(void*, const uint32_t*, size_t);
  void (*releaseGsub)(void*);
  bool (*hasGlyph)(void*, uint32_t);
  void (*deinit)(void*);
  void (*lineMetrics)(void*, bool, int32_t*, int32_t*);
} XpVectorServices;
const void* xp_vector_glyph(const XpVectorServices*, uint32_t);
int8_t xp_vector_kern(const XpVectorServices*, uint32_t, uint32_t);
typedef struct {
  void* ctx;
  const XpVectorSource* sources;
  uint32_t* size;
  bool* loaded;
  const bool* locked;
  XpVectorServices (*face)(void*, uint8_t);
  void (*setup)(void*);
} XpVectorFamily;
void xp_vector_init(const XpVectorServices*, const XpVectorSource*);
bool xp_vector_load(const XpVectorFamily*, uint16_t pointSize, bool twoBit, size_t cap, uint16_t maximum);
void xp_vector_clear(const XpVectorFamily*, bool resident);
bool xp_vector_prewarm(const XpVectorFamily*, const uint8_t*, size_t length, bool reset);
bool xp_vector_coverage(const XpVectorServices*, uint32_t);
void xp_vector_metrics(const XpVectorServices*, void* fontData);
// The manager owns fonts returned by create: failed loads call destroy;
// successful loads transfer ownership into the existing loaded-font container.
// Names and views are synchronous loans that expire on container mutation.
#ifdef __cplusplus
enum class XpFontLoadMode : uint8_t { Replace, ExtraSize, Variant };
enum class XpFontManagerEvent : uint32_t {
  Path = 1,
  Allocation = 2,
  Load = 3,
  Collision = 4,
  Loaded = 5,
  EmptyFamily = 6
};
#endif
typedef struct {
  void* font;
  int32_t id;
  uint8_t size;
} XpFontLoaded;
typedef struct {
  const char* name;
  size_t nameLen;
  const XpFontLoaded* entries;
  size_t count;
} XpFontManagerView;
typedef struct {
  const char* name;
  size_t nameLen;
  const char* root;
  XpFontFiles files;
  bool loose;
} XpFontFamilyFiles;
typedef struct {
  void* ctx;
  XpFontManagerView (*view)(void*);
  void* (*create)();
  bool (*load)(void*, const char*);
  void (*destroy)(void*);
  uint32_t (*info)(void*, uint8_t* styles);
  bool (*collision)(void*, int32_t);
  void (*registerSd)(void*, int32_t, void*);
  void (*push)(void*, XpFontLoaded);
  void (*insertFont)(void*, int32_t, void*);
  void (*removeFont)(void*, int32_t);
  void (*erase)(void*, size_t);
  void (*clearFallbacks)(void*);
  void (*clearSd)(void*);
  void (*clear)(void*);
  void (*family)(void*, const char*, size_t, uint8_t);
  void (*diagnostic)(void*, uint32_t, const char*, int32_t, uint32_t, uint32_t);
} XpFontManagerServices;
void xp_font_manager_destroy(const XpFontManagerServices*);
void xp_font_manager_unload(const XpFontManagerServices*, int32_t id, bool all);
int32_t xp_font_manager_get(const XpFontManagerServices*, const char* family, size_t nameLen, uint8_t size);
int32_t xp_font_manager_load(const XpFontManagerServices*, const XpFontFamilyFiles*, uint8_t size, int32_t id,
                             uint8_t mode);
bool xp_font_canonical(const char* name, const uint8_t* family, size_t length, uint8_t size);
// Synchronous transfer jobs. Callbacks and context must remain live and cannot reenter.
// Buffers are borrowed for one callback. Flash scratch is exactly 4096 bytes, allocated
// and freed by the paired C++ callbacks; no Rust allocation is retained.
typedef struct {
  uint8_t captured[24];
  size_t name_len;
  size_t magic_matched;
  bool capturing;
  bool mismatch;
} XpBoardScanner;
void xp_board_scan(XpBoardScanner* state, const uint8_t* data, size_t len, const uint8_t* tag, size_t tag_len);
enum XpFlashOp {
  XP_FLASH_OPEN,
  XP_FLASH_CLOSE,
  XP_FLASH_SIZE,
  XP_FLASH_CHIP,
  XP_FLASH_ALLOCATE,
  XP_FLASH_FREE,
  XP_FLASH_READ,
  XP_FLASH_SHA_INIT,
  XP_FLASH_SHA_UPDATE,
  XP_FLASH_SHA_FINISH,
  XP_FLASH_SHA_FREE,
  XP_FLASH_PARTITION,
  XP_FLASH_ERASE,
  XP_FLASH_WRITE,
  XP_FLASH_SWITCH,
  XP_FLASH_PROGRESS,
  XP_FLASH_DELAY,
  XP_FLASH_PARTITION_SIZE
};
typedef struct {
  void* ctx;
  size_t (*operation)(void*, uint32_t op, size_t a, size_t b, uint8_t* bytes);
  // Events 0..11: validation; 12..20: partition/open/transfer/switch. Text is
  // a borrowed NUL-terminated board name for event 8, otherwise null.
  void (*diagnostic)(void*, uint32_t event, size_t a, size_t b, size_t c, size_t d, const uint8_t* text);
  const uint8_t* tag;
  size_t tag_len;
} XpFlashServices;
uint32_t xp_flash_validate(const XpFlashServices* services, size_t partition_size);
uint32_t xp_flash_run(const XpFlashServices* services, bool already_validated);

bool xp_sensitive_path(const uint8_t* path, size_t len, uint8_t* scratch);
bool xp_short_alias(const uint8_t* path, size_t len);
bool xp_plugin_path_candidate(const uint8_t* path, size_t len);

enum XpHttpMode { XP_HTTP_FETCH, XP_HTTP_CATALOG, XP_HTTP_FORM, XP_HTTP_DOWNLOAD, XP_HTTP_POST_FILE };
typedef struct {
  size_t bytes, total;
  int32_t status;
  uint32_t mode;
  bool capture, known, aborted, stopped, complete, same_origin;
} XpHttpState;
enum XpHttpOp {
  XP_HTTP_REMOVE,
  XP_HTTP_OPEN,
  XP_HTTP_CLOSE,
  XP_HTTP_REPLACE,
  XP_HTTP_WRITE,
  XP_HTTP_PROGRESS,
  XP_HTTP_SINK,
  XP_HTTP_APPEND,
  XP_HTTP_RESOLVE,
  XP_HTTP_SAME_ORIGIN,
  XP_HTTP_LAST_STATUS,
  XP_HTTP_LAST_REDIRECTED,
  XP_HTTP_GUARD_ENTER,
  XP_HTTP_GUARD_EXIT
};
typedef struct {
  void* ctx;
  size_t (*operation)(void*, uint32_t op, size_t a, size_t b, const uint8_t* bytes);
  int32_t (*request)(void*, XpHttpState*);
  void (*diagnostic)(void*, uint32_t event, size_t a, size_t b);
} XpHttpServices;
// Mode 0 simple GET; 1 catalog GET; 2 form POST; 3 file GET; 4 file POST.
// A request may call xp_http_chunk synchronously; all other reentry is forbidden.
bool xp_http_redirect(int32_t status);
// Authorization choice: 0 none, 1 basic, 2 bearer. Caller headers use same_origin.
uint32_t xp_http_auth(bool same_origin, size_t bearer_len, size_t user_len, size_t password_len);
uint32_t xp_http_run(const XpHttpServices* services, XpHttpState* state);
bool xp_http_chunk(const XpHttpServices* services, XpHttpState* state, const uint8_t* data, size_t len);

typedef struct {
  bool ok, existed;
} XpDavState;
enum XpDavOp {
  XP_DAV_EXISTS,
  XP_DAV_OPEN,
  XP_DAV_CLOSE,
  XP_DAV_DIRECTORY,
  XP_DAV_SIZE,
  XP_DAV_NEXT,
  XP_DAV_NAME,
  XP_DAV_READ,
  XP_DAV_WRITE,
  XP_DAV_AVAILABLE,
  XP_DAV_REMOVE,
  XP_DAV_RMDIR,
  XP_DAV_MKDIR,
  XP_DAV_RENAME,
  XP_DAV_OPEN_WRITE,
  XP_DAV_REMOVE_BOOK,
  XP_DAV_CLEAR_CACHE,
  XP_DAV_MOVE_BOOK,
  XP_DAV_STREAM,
  XP_DAV_WATCHDOG,
  XP_DAV_YIELD,
  XP_DAV_LENGTH,
  XP_DAV_IS_OPEN
};
// Paths have writable NUL-terminated storage; Rust temporarily terminates a parent
// component during exists(). Callbacks cannot retain those pointers or reenter.
typedef struct {
  void* ctx;
  size_t (*operation)(void*, uint32_t, size_t, size_t, const uint8_t*);
  void (*response)(void*, uint32_t, const uint8_t*, const uint8_t*);
  void (*content)(void*, uint32_t, const uint8_t*, size_t);
  void (*header)(void*, const uint8_t*, const uint8_t*);
  void (*diagnostic)(void*, uint32_t, size_t, size_t);
  uint8_t* path;
  size_t path_len;
  uint8_t* destination;
  size_t destination_len;
  // NUL-terminated upload path, or null if unavailable; cleanup still closes the file.
  const uint8_t* temp;
  uint32_t depth;
  bool overwrite, body;
} XpDavServices;
bool xp_dav_handle(const XpDavServices*, const XpDavState*, uint32_t method);
void xp_dav_raw(const XpDavServices*, XpDavState*, uint32_t event, const uint8_t*, size_t len, size_t total);
// out capacity >= len+2. Input and output cannot overlap.
size_t xp_dav_normalize(const uint8_t*, size_t len, uint8_t* out);
size_t xp_dav_destination_offset(const uint8_t*, size_t len);
// Bit 0: depth is positive; bit 1: overwrite is allowed.
uint32_t xp_dav_headers(const uint8_t* depth, size_t depth_len, const uint8_t* overwrite, size_t overwrite_len);

typedef struct {
  uint8_t pending[16], id[64];
  size_t len;
} XpLcpParse;
enum XpLcpOp {
  XP_LCP_LOAD,
  XP_LCP_ID_LEN,
  XP_LCP_PRIVATE_LEN,
  XP_LCP_EXPORT_ID,
  XP_LCP_KEYGEN,
  XP_LCP_APPEND_REQUEST,
  XP_LCP_APPEND_PUBLIC,
  XP_LCP_POST,
  XP_LCP_SET_ID,
  XP_LCP_SET_PRIVATE,
  XP_LCP_SAVE,
  XP_LCP_FORGET,
  XP_LCP_PRIVATE,
  XP_LCP_DECODE,
  XP_LCP_UNWRAP
};
typedef struct {
  void* ctx;
  size_t (*operation)(void*, uint32_t, size_t, uint8_t*);
  bool (*parse)(void*, XpLcpParse*);
  void (*diagnostic)(void*, uint32_t);
  bool* loaded;
  const uint8_t* release;
  size_t release_len;
} XpLcpServices;
void xp_lcp_json(XpLcpParse*, bool key, const uint8_t*, size_t len);
bool xp_lcp_existing(const XpLcpServices*, bool* not_enrollable);
bool xp_lcp_ensure(const XpLcpServices*, bool* not_enrollable);
void xp_lcp_forget(const XpLcpServices*);
bool xp_lcp_unwrap(const XpLcpServices*, uint8_t out[32]);

// System item name only; callers own dotfile and path-component policy.
// data borrows len readable bytes; null is allowed only for zero length.
bool xp_system_item_name(const uint8_t* data, size_t len);

#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
// Serialized render-owner loan; clear before freeing/reusing the caller buffer.
// Cache reset/release does not end the loan and never frees borrowed storage.
void xp_assets_image_read_window(uint8_t* data, size_t len);
void xp_strip_packed_row(const XpImageTarget* target, int32_t x, int32_t y, int32_t width, const uint8_t* row,
                         size_t len);
// Commands borrow page bytes; invalidate on page/font/origin/orientation change.
// Storage must be pointer-aligned; bounds needs one entry per page element.
// Prepare clears valid on failure. Replay requires valid and unchanged storage/bounds.
typedef struct {
  int32_t start, end;
} XpPageRowBounds;
typedef struct {
  void* storage;
  size_t capacity_bytes;
  size_t count;
  uint8_t valid;
} XpPagePlan;
uint32_t xp_page_plan_render(const uint8_t* arena, size_t size, const XpNativeProvider* provider,
                             const XpPageRenderServices* services, XpGlyphPaint* paint, int32_t font, int32_t x,
                             int32_t y, uint8_t mode, XpPageRowBounds* bounds, size_t count, XpPagePlan* plan,
                             uint8_t prepare);
#endif

// Book adapters: call-local borrowed buffers; callbacks may not retain pointers
// or reenter. TXT input/output each contain 8192 initialized writable bytes,
// disjoint from title/tag and each other. Status: 0 success, -1 read, -2 write.
size_t xp_book_txt_escaped_size(const uint8_t* text, size_t len);
void xp_book_txt_escape(const uint8_t* text, size_t len, void* context, void (*emit)(void*, const uint8_t*, size_t));
int32_t xp_book_txt_stream(const uint8_t* title, size_t title_len, const uint8_t* tag, size_t tag_len, uint8_t* input,
                           uint8_t* output, void* context, int32_t (*read)(void*, uint8_t*, size_t),
                           size_t (*write)(void*, const uint8_t*, size_t));

// XTC image output preserves write/yield order and ignores short writes, as the
// original facade did. Buffers are disjoint and borrowed for this call only.
typedef struct {
  uint16_t width, height;
  float scale;
} XpBookXtcThumbnail;
void xp_book_xtc_thumbnail(uint16_t width, uint16_t height, int32_t target, XpBookXtcThumbnail* out);
void xp_book_xtc_image(const uint8_t* pixels, size_t len, uint16_t width, uint16_t height, uint8_t depth, uint8_t* row,
                       size_t row_len, const XpBookXtcThumbnail* plan, void* context,
                       size_t (*write)(void*, const uint8_t*, size_t), void (*yield_now)(void*));

// Wrapped-key platform leaves. All pointers are borrowed for the call; cipher
// has a 32-byte KEK, 12-byte IV, 16-byte tag and key_len-byte input/output.
typedef struct {
  uint8_t* output;
  const uint8_t *input, *kek, *iv;
  uint8_t* tag;
  const uint8_t* aad;
  size_t key_len, aad_len;
  bool encrypt;
} XpBookKeyCipher;
typedef struct {
  void* context;
  bool (*secret)(void*, uint8_t*);
  bool (*hash)(void*, const uint8_t*, size_t, uint8_t*);
  void (*random)(void*, uint8_t*, size_t);
  bool (*cipher)(void*, const XpBookKeyCipher*);
  int32_t (*read)(void*, uint8_t*, size_t);
  bool (*write)(void*, const uint8_t*, size_t);
  bool (*replace)(void*);
} XpBookKeyServices;
int32_t xp_book_key_write(const uint8_t* key, size_t len, int64_t expires, const XpBookKeyServices* services);
int32_t xp_book_key_read(uint8_t* key, int64_t* expires, const XpBookKeyServices* services);

// XTC diagnostics use event,a,b,c,d (d is unused); all calls are synchronous.
// Events 1-10 describe header/table validation using the inspected state. Event 7
// adds file size in a:b (low:high). Events 11-14 and 20 carry an XtcError in a.
// Events 15/16 report open/reopen. Events 17/21 carry page index a and offset b:c.
// Events 18/22 carry page index a; 19 carries chapter count a; 23 carries page a
// and magic b; 24 carries page a, declared bytes b and expected bytes c;
// 25/26 carry needed/expected bytes a and available/read bytes b.
// XTC reader state has Rust Cell-compatible scalar layout. Calls are serialized;
// callbacks may inspect it for diagnostics, but may not mutate it or reenter.
typedef struct {
  XpXtcHeader header;
  uint16_t width, height;
  uint8_t depth;
  bool opened, has_chapters, chapters_loaded;
  int32_t error;
} XpBookXtcState;
typedef struct {
  void* context;
  bool (*open)(void*);
  bool (*is_open)(void*);
  void (*close)(void*);
  int32_t (*read)(void*, uint8_t*, size_t);
  bool (*seek)(void*, uint64_t);
  uint64_t (*size)(void*);
  void (*metadata)(void*, uint32_t, const uint8_t*, size_t);
  size_t (*chapters)(void*, uint32_t, size_t, const uint8_t*, uint16_t, uint16_t);
  void (*diagnostic)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
} XpBookXtcServices;
typedef struct {
  void* context;
  uint8_t* (*allocate)(void*, size_t);
  void (*emit)(void*, const uint8_t*, size_t, size_t);
} XpBookXtcStream;
int32_t xp_book_xtc_open(const XpBookXtcState*, const XpBookXtcServices*);
void xp_book_xtc_close(const XpBookXtcState*, const XpBookXtcServices*);
void xp_book_xtc_chapters(const XpBookXtcState*, const XpBookXtcServices*);
bool xp_book_xtc_page_info(const XpBookXtcState*, const XpBookXtcServices*, uint32_t index, XpXtcEntry*);
size_t xp_book_xtc_load(const XpBookXtcState*, const XpBookXtcServices*, uint32_t index, uint8_t*, size_t);
int32_t xp_book_xtc_stream(const XpBookXtcState*, const XpBookXtcServices*, uint32_t index, size_t,
                           const XpBookXtcStream*);

// ZIP extraction uses the existing C allocator and InflateStream scratch owner.
// Callback spans are borrowed; inflater fill context is valid only until return.
typedef size_t (*XpBookZipFill)(void*, const uint8_t**);
typedef struct {
  void* context;
  int32_t (*read)(void*, uint8_t*, size_t);
  uint8_t* (*allocate)(void*, size_t);
  void (*release)(void*, uint8_t*);
  bool (*init)(void*, bool, XpBookZipFill, void*);
  bool (*exact)(void*, uint8_t*, size_t);
  int32_t (*inflate)(void*, uint8_t*, size_t, size_t*);
  size_t (*write)(void*, const uint8_t*, size_t);
  void (*diagnostic)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
} XpBookZipServices;
uint8_t* xp_book_zip_memory(const XpBookZipServices*, uint16_t method, uint32_t compressed, uint32_t inflated,
                            bool trailing_nul, size_t* size);
bool xp_book_zip_stream(const XpBookZipServices*, uint16_t method, uint32_t compressed, uint32_t inflated, size_t chunk,
                        bool early_stop);

// Copy while the source reports available bytes; retain legacy ignored writes.
// Negative reads fail without exposing an invalid span to the destination.
bool xp_book_copy_available(uint8_t* buffer, size_t capacity, void* context, bool (*available)(void*),
                            int32_t (*read)(void*, uint8_t*, size_t), size_t (*write)(void*, const uint8_t*, size_t));

// Values match book_io::txt::CacheAction.
#ifdef __cplusplus
enum class XpBookTxtCacheAction : uint32_t {
  Exists = 0,
  Mkdir = 1,
  Invalidate = 2,
  Begin = 3,
  BeginOpf = 4,
  Spine = 5,
  EndOpf = 6,
  BeginToc = 7,
  Toc = 8,
  EndToc = 9,
  End = 10,
  Book = 11,
  Cleanup = 12,
  Reload = 13,
  Complete = UINT32_MAX
};
#endif
// TXT cache control selects existing C++ facade operations. UINT32_MAX means
// success; otherwise the result identifies the failed action for diagnostics.
uint32_t xp_book_txt_build(void* context, bool (*control)(void*, uint32_t));
void xp_book_txt_find_cover(void* context, bool (*candidate)(void*, const uint8_t*, const uint8_t*));

// Borrowed text only; trim outputs must be disjoint and writable. XML names are
// NUL-terminated (or null); the result aliases the name or a static empty string.
bool xp_book_ascii_space(uint8_t byte);
void xp_book_ascii_trim(const uint8_t* data, size_t len, size_t* start, size_t* end);
const uint8_t* xp_book_xml_local(const uint8_t* name);
uint32_t xp_book_utf8_next(const uint8_t* data, size_t* consumed);
// Loan result: 0 allowed, 1 clock unverified, 2 expired. No clock call for zero expiry.
uint32_t xp_book_loan(int64_t expires, void* context, int64_t (*clock)(void*));

// Values match book_io::txt; control receives the action for its entry point.
// Only JpegThumb/PngThumb use the int32 payloads (width, height); otherwise zero.
#ifdef __cplusplus
enum class XpBookTxtValidateAction : uint32_t {
  Exists = 0,
  OpenHtml = 1,
  CloseHtml = 2,
  OpenRaw = 3,
  CloseRaw = 4,
  Invalidate = 5
};
enum class XpBookTxtConvertAction : uint32_t {
  Exists = 0,
  Open = 1,
  Jpeg = 2,
  Png = 3,
  JpegThumb = 4,
  PngThumb = 5,
  Close = 6,
  Remove = 7
};
enum class XpBookTxtImageKind : uint32_t { Unsupported = 0, Bmp = 1, Jpeg = 2, Png = 3 };
// SizeChanged carries the source byte size; the other events carry zero.
enum class XpBookTxtFileDiagnostic : uint32_t { VersionMismatch = 0, SizeChanged = 1, InvalidCache = 2 };
#endif

// All callbacks are nullable in the ABI. Missing required callbacks return false
// before I/O. Validate requires control/read, plus size when cached > 0; write is
// unused and diagnostic may be null for silence. Convert requires control, plus
// read/write for BMP; size/diagnostic and non-BMP read/write are unused.
typedef struct {
  void* context;
  bool (*control)(void*, uint32_t, int32_t, int32_t);
  int32_t (*read)(void*, uint8_t*, size_t);
  size_t (*write)(void*, const uint8_t*, size_t);
  size_t (*size)(void*);
  void (*diagnostic)(void*, uint32_t, size_t);
} XpBookTxtFileServices;
bool xp_book_txt_validate(const XpBookTxtFileServices*, size_t cached, const uint8_t* tag, size_t len);
bool xp_book_txt_convert(const XpBookTxtFileServices*, uint32_t kind, int32_t height);

// Dictionary callbacks borrow all handles and buffers for the call; no pointer is retained.
typedef struct {
  void* context;
  int32_t (*read)(void*, uint8_t*, size_t);
  size_t (*write)(void*, const uint8_t*, size_t);
  bool (*seek)(void*, size_t);
  size_t (*tell)(void*);
  size_t (*size)(void*);
} XpDictionaryFile;
bool xp_dictionary_html(const uint8_t*, size_t, void*, size_t (*)(void*, const uint8_t*, size_t));
uint8_t xp_dictionary_html_limit(size_t pages, size_t elements, size_t added, size_t free, size_t largest);
bool xp_dictionary_html_ready(size_t free, size_t largest);
typedef struct {
  uint32_t dataOffset, totalSize;
  uint16_t chunkLength;
  bool valid;
} XpDictZipInfo;
typedef struct {
  void* context;
  bool (*reserve)(void*, size_t);
  void (*push)(void*, uint32_t);
} XpDictZipTable;
uint8_t xp_dictzip_parse(const XpDictionaryFile*, XpDictZipInfo*, const XpDictZipTable*);
uint8_t xp_dictzip_range(const XpDictZipInfo*, const uint32_t*, size_t, uint32_t, uint32_t, void*,
                         uint8_t (*)(void*, uint32_t, uint32_t, uint32_t, uint32_t));
size_t xp_dictzip_refill(const XpDictionaryFile*, uint32_t*, bool*, uint8_t*, const uint8_t**);
uint8_t xp_dictzip_slice(void*, uint8_t (*)(void*, uint8_t*, size_t), uint8_t*, uint32_t, uint32_t,
                         const XpDictionaryFile*);
typedef struct {
  uint32_t sampleCount, sourceFileSize, entryCount;
  bool valid;
} XpDictionaryHeader;
void xp_dictionary_header(const XpDictionaryFile*, uint32_t, uint32_t, XpDictionaryHeader*);
typedef struct {
  uint32_t sampleCount, entryCount, position;
} XpDictionaryScan;
uint8_t xp_dictionary_scan(const XpDictionaryFile*, const XpDictionaryFile*, uint32_t, uint32_t, uint8_t*, void*,
                           void (*)(void*), XpDictionaryScan*);
typedef struct {
  uint32_t offset, size;
  bool found, readError;
} XpDictionaryLocation;
typedef struct {
  const XpDictionaryFile *idx, *qidx, *syn, *sidx;
  uint32_t idxSize, sampleCount, entryCount, synSize, synSampleCount;
  uint8_t* word;
  void* context;
  bool (*append)(void*, const uint8_t*, XpDictionaryLocation);
  void (*seekError)(void*, bool, uint32_t);
} XpDictionarySearch;
bool xp_dictionary_search(const XpDictionarySearch*, const uint8_t*, size_t, bool);
void xp_dictionary_clean_bounds(const uint8_t*, size_t, size_t*, size_t*);
void xp_dictionary_lower(uint8_t*, size_t);
void xp_dictionary_stems(const uint8_t*, size_t, void*, void (*)(void*, size_t, uint8_t));
uint8_t xp_dictionary_ifo(const uint8_t*, size_t);
typedef struct {
  void* context;
  bool (*open)(void*, uint8_t, const uint8_t*);
  uint8_t (*next)(void*, uint8_t, uint8_t*);
  void (*close)(void*, uint8_t);
  bool (*exists)(void*, const uint8_t*);
  void (*emit)(void*, const uint8_t*, size_t, const uint8_t*, size_t);
  void (*diagnostic)(void*, uint8_t, const uint8_t*);
} XpDictionaryRegistry;
bool xp_dictionary_registry(const XpDictionaryRegistry*, const uint8_t*, size_t, bool);
typedef struct {
  void* context;
  const XpDictionaryFile* file;
  uint32_t offset, size;
  bool found, plain, html, append;
  size_t length, capacity;
  size_t (*heap)();
  bool (*open)(void*, uint8_t);
  uint8_t (*extract)(void*, uint32_t, uint32_t);
  void (*closeTmp)(void*);
  uint8_t* (*resize)(void*, size_t, bool);
  void (*diagnostic)(void*, uint8_t, uint32_t, uint32_t, uint32_t);
} XpDictionaryDefinition;
uint8_t xp_dictionary_definition(const XpDictionaryDefinition*);
typedef struct {
  const uint8_t* data;
  size_t length;
} XpDictionaryView;
typedef struct {
  size_t count;
  bool lowMemory;
} XpDictionaryGroup;
typedef struct {
  void* context;
  size_t wordBytes, stringSize, freeBytes, largestBlock;
  bool open, entries, synonyms, html;
  bool (*prepare)(void*, uint8_t);
  XpDictionaryView (*view)(void*, uint8_t, size_t);
  XpDictionaryGroup (*group)(void*);
  bool (*probe)(void*, const uint8_t*, size_t, bool);
  void (*close)(void*);
  void (*title)(void*, bool);
  XpDictionaryLocation (*location)(void*, size_t);
  uint8_t (*read)(void*, size_t, bool);
  void (*output)(void*, uint8_t, const uint8_t*, size_t);
  size_t (*heap)();
  void (*diagnostic)(void*, uint8_t, uint32_t, uint32_t);
} XpDictionaryLookup;
uint8_t xp_dictionary_lookup(const XpDictionaryLookup*);
typedef struct {
  void* context;
  bool *plain, *syn, *html;
  void (*reset)(void*);
  bool (*resolve)(void*);
  bool (*exists)(void*, uint8_t);
  uint8_t (*ifo)(void*);
  size_t (*length)(void*);
  bool (*allocate)(void*);
  void (*finish)(void*);
  void (*diagnostic)(void*, uint8_t, size_t);
} XpDictionaryOpen;
bool xp_dictionary_open(const XpDictionaryOpen*);
typedef struct {
  void* context;
  bool open, syn;
  bool (*stale)(void*, bool);
  uint8_t (*build)(void*, bool);
  void (*diagnostic)(void*);
} XpDictionaryIndex;
uint8_t xp_dictionary_index(const XpDictionaryIndex*, bool);
typedef struct {
  uint32_t idxSize, sampleCount, entryCount, synSize, synSampleCount;
  bool synOpened, synFailed;
} XpDictionarySessionState;
typedef struct {
  void* context;
  XpDictionarySessionState* state;
  bool open, syn;
  bool (*openFile)(void*, uint8_t);
  uint32_t (*size)(void*, uint8_t);
  XpDictionaryHeader (*header)(void*, uint8_t);
  void (*close)(void*, uint8_t);
  void (*diagnostic)(void*);
} XpDictionarySessionIo;
bool xp_dictionary_session(const XpDictionarySessionIo*, bool);
typedef struct {
  void* context;
  size_t count, capacity, bytes, freeBytes, largestBlock;
  XpDictionaryLocation (*location)(void*, size_t);
  bool (*allocate)(void*, size_t);
  void (*finish)(void*);
} XpDictionaryAppend;
uint8_t xp_dictionary_append(const XpDictionaryAppend*, uint32_t, uint32_t);

#ifdef __cplusplus
}
#endif
