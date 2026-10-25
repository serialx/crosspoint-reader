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

// Paragraph breaking callbacks are synchronous and may not reenter layout. The
// context and callback record outlive the call; no pointer is retained by Rust.
// count/width describe one sequence. Successful split inserts exactly one word
// and updates widths; other metric callbacks do not mutate the sequence.
// ruby intervals use exclusive ends. emit copies one break, returning 0 on OOM.
// Both entry points return 1 on success, 0 on allocation/size/output failure.
// Optimal allocates int32_t[N] + size_t[N] with the paragraph allocator; greedy
// has no Rust allocations. Caller owns widths, split mutation and output storage.
typedef struct {
  void* context;
  size_t (*count)(void*);
  uint16_t (*width)(void*, size_t);
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

#ifdef __cplusplus
}
#endif
