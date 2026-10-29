#pragma once
#include <stddef.h>
#include <stdint.h>

#include "xp_ffi.h"
#ifdef __cplusplus
extern "C" {
#endif
// Synchronous borrowed spans, readable for len bytes (null only for zero length).
// All callbacks run inline on the calling task; the caller serializes shared owners.
// Non-diagnostic callbacks may nest Rust calls only with distinct contexts, without
// invalidating active borrows or owners. Diagnostic sinks must not enter Rust or
// retain pointers. Services, ctx and required callbacks stay live until the entry
// point returns. Diagnostic slots are optional (null means silent);
// all other callback slots are required. Input spans stay readable for the entry
// point. Callback spans are borrowed only for that callback unless stated below;
// receivers copy rather than retain them.
typedef struct {
  const uint8_t* data;
  size_t len;
} XpBookBorrow;
uint8_t xp_book_bidi_should_shape(const uint8_t* data);
uint8_t xp_book_bidi_word_count(size_t count);
uint8_t xp_book_bidi_word_spans(const uint8_t* data, size_t len, const uint16_t* offsets, size_t count, size_t output);
uint8_t xp_book_xtc_progress(uint8_t loaded, uint32_t page, uint32_t pages);
XpBookBorrow xp_book_xtc_title(XpBookBorrow path, XpBookBorrow metadata);
uint32_t xp_book_txt_image_kind(XpBookBorrow path);
const char* xp_book_txt_cache_tag(XpBookBorrow path);
// get visits indices in order until a duplicate is found; its path lives until the
// next callback. No cleanup callback is issued; base/path must remain unchanged.
uint8_t xp_book_discover_css(XpBookBorrow base, XpBookBorrow path, size_t count, void* ctx,
                             XpBookBorrow (*get)(void* ctx, size_t index));
typedef enum XpBookDocumentKind {
  XP_BOOK_DOCUMENT_CONTAINER = 1,  // container.xml; metadata flag ignored.
  XP_BOOK_DOCUMENT_NCX = 2,        // NCX TOC; metadata flag ignored.
  XP_BOOK_DOCUMENT_NAV = 3,        // XHTML TOC; metadata flag ignored.
  XP_BOOK_DOCUMENT_OPF = 5,        // OPF; nonzero metadata flag stops after metadata.
} XpBookDocumentKind;
typedef enum XpBookDocumentOp {
  XP_BOOK_DOCUMENT_FIND = 0,           // arg=0; find OPF path; result=boolean.
  XP_BOOK_DOCUMENT_BASE = 1,           // arg=0; set OPF/nav base; result ignored.
  XP_BOOK_DOCUMENT_SIZE = 2,           // arg=0; retain selected file size; result=boolean, not byte count.
  XP_BOOK_DOCUMENT_SETUP = 3,          // arg=0; create/setup selected parser; result=boolean.
  XP_BOOK_DOCUMENT_READ = 4,           // arg=chunk bytes; stream selected file; result=boolean.
  XP_BOOK_DOCUMENT_GUIDE_LOAD = 5,     // arg=0; retain guide HTML/base; result=boolean.
  XP_BOOK_DOCUMENT_GUIDE_ASSIGN = 6,   // arg=0; normalize and assign guide image path; result ignored.
  XP_BOOK_DOCUMENT_GUIDE_RELEASE = 7,  // arg=0; release guide HTML/base/image; result ignored.
  XP_BOOK_DOCUMENT_CSS = 8,            // arg=0; transfer parsed CSS paths; result ignored.
} XpBookDocumentOp;
typedef enum XpBookDocumentDiagnostic {
  XP_BOOK_DOCUMENT_LOG_CONTAINER_SIZE = 0,   // Container size unavailable.
  XP_BOOK_DOCUMENT_LOG_CONTAINER_READ = 1,   // Container read failed.
  XP_BOOK_DOCUMENT_LOG_CONTAINER_ROOT = 2,   // Container rootfile absent.
  XP_BOOK_DOCUMENT_LOG_FIND_OPF = 3,         // OPF path absent.
  XP_BOOK_DOCUMENT_LOG_PARSING_OPF = 4,      // Parsing OPF; path is in ctx.
  XP_BOOK_DOCUMENT_LOG_OPF_SIZE = 5,         // OPF size unavailable.
  XP_BOOK_DOCUMENT_LOG_OPF_SETUP = 6,        // OPF setup failed.
  XP_BOOK_DOCUMENT_LOG_OPF_READ = 7,         // OPF read failed.
  XP_BOOK_DOCUMENT_LOG_METADATA_PARSED = 8,  // Metadata-only parse complete.
  XP_BOOK_DOCUMENT_LOG_GUIDE_FALLBACK = 9,   // Guide fallback; guide path is in ctx.
  XP_BOOK_DOCUMENT_LOG_GUIDE_FOUND = 10,     // Guide image found; assigned cover is in ctx.
  XP_BOOK_DOCUMENT_LOG_OPF_PARSED = 11,      // OPF parse complete.
  XP_BOOK_DOCUMENT_LOG_NO_NCX = 12,          // NCX path absent.
  XP_BOOK_DOCUMENT_LOG_PARSING_NCX = 13,     // Parsing NCX; path is in ctx.
  XP_BOOK_DOCUMENT_LOG_NCX_SIZE = 14,        // NCX size unavailable.
  XP_BOOK_DOCUMENT_LOG_NCX_SETUP = 15,       // NCX setup failed.
  XP_BOOK_DOCUMENT_LOG_NCX_READ = 16,        // NCX read failed.
  XP_BOOK_DOCUMENT_LOG_NO_NAV = 17,          // Nav path absent.
  XP_BOOK_DOCUMENT_LOG_PARSING_NAV = 18,     // Parsing nav; path is in ctx.
  XP_BOOK_DOCUMENT_LOG_NAV_SIZE = 19,        // Nav size unavailable.
  XP_BOOK_DOCUMENT_LOG_NAV_SETUP = 20,       // Nav setup failed.
  XP_BOOK_DOCUMENT_LOG_NAV_READ = 21,        // Nav read failed.
  XP_BOOK_DOCUMENT_LOG_NCX_PARSED = 22,      // NCX parse complete.
  XP_BOOK_DOCUMENT_LOG_NAV_PARSED = 23,      // Nav parse complete.
} XpBookDocumentDiagnostic;
typedef enum XpBookDocumentField {
  XP_BOOK_DOCUMENT_CONTAINER_PATH = 0,  // Container value/assign rootfile path; not an OPF title.
  // value: guide HTML during OPF, selected path during NCX/nav; assign: OPF guide image.
  // Other OPF fields use XpPkgField. Guide HTML stays readable until GUIDE_RELEASE.
  XP_BOOK_DOCUMENT_AUXILIARY = 100,
} XpBookDocumentField;
// SIZE and SETUP must succeed before READ. ctx releases the parser on every return;
// no parser-release callback is issued.
// GUIDE_RELEASE follows each successful GUIDE_LOAD, even if no image is found.
// FIND may parse a container with a distinct context under the common nesting rules.
// Required: call, value, assign. Optional: diagnostic (event only, no payload).
typedef struct XpBookDocumentServices {
  void* ctx;
  size_t (*call)(void* ctx, uint32_t operation, size_t arg);
  // Parser values remain readable until parser mutation/release; guide HTML until GuideRelease.
  XpBookBorrow (*value)(void* ctx, uint32_t field);
  void (*assign)(void* ctx, uint32_t field, XpBookBorrow value);
  void (*diagnostic)(void* ctx, uint32_t event);
} XpBookDocumentServices;
// kind is XpBookDocumentKind (same values as XP_PKG_* passes); result=boolean.
uint8_t xp_book_document(const XpBookDocumentServices* services, uint32_t kind, uint8_t metadataOnly);
typedef enum XpBookCssOp {
  XP_BOOK_CSS_FILE_CLEAR = 0,  // index=arg=0; clear parser; result ignored.
  // index=0, arg=count; result=null or aligned XpBookCssEntry[count] pointer cast to size_t.
  // Null means allocation failure; parsing continues without deduplication.
  // The table stays immutable and readable until xp_book_css_parse_files returns.
  XP_BOOK_CSS_FILE_DEDUP = 1,
  XP_BOOK_CSS_FILE_HEAP = 2,        // index=file, arg=0; result=free heap bytes.
  XP_BOOK_CSS_FILE_SIZE = 3,        // index=file, arg=0; result=inflated bytes, or 0 on failure.
  XP_BOOK_CSS_FILE_OPEN_WRITE = 4,  // index=file, arg=0; create retained temporary file; result=boolean.
  XP_BOOK_CSS_FILE_STREAM = 5,      // index=file, arg=chunk bytes; stream CSS to temporary file; result=boolean.
  XP_BOOK_CSS_FILE_CLOSE = 6,       // index=file, arg=0; close temporary file; result ignored.
  XP_BOOK_CSS_FILE_REMOVE = 7,      // index=file, arg=0; remove temporary path; result ignored.
  XP_BOOK_CSS_FILE_OPEN_READ = 8,   // index=file, arg=0; reopen temporary file for reading; result=boolean.
  XP_BOOK_CSS_FILE_PARSE = 9,       // index=file, arg=0; parse temporary file; result=XpBookCssParseStatus.
  XP_BOOK_CSS_FILE_DROP_TEMP = 10,  // index=file, arg=0; release temporary file/path owner; result ignored.
  XP_BOOK_CSS_FILE_EMPTY = 11,      // index=arg=0; result=boolean: parser has no rules.
  XP_BOOK_CSS_FILE_SAVE = 12,       // index=0, arg=boolean complete; save cache; result=boolean.
} XpBookCssOp;
typedef enum XpBookCssParseStatus {
  XP_BOOK_CSS_PARSE_COMPLETE = 0,  // Complete parse.
  XP_BOOK_CSS_PARSE_PARTIAL = 1,   // Partial usable rules.
  XP_BOOK_CSS_PARSE_ERROR = 2,     // Failed parse.
} XpBookCssParseStatus;
typedef enum XpBookCssDiagnostic {
  XP_BOOK_CSS_LOG_NO_FILES = 0,        // index=value=0; no files.
  XP_BOOK_CSS_LOG_COUNT = 1,           // index=0, value=file count.
  XP_BOOK_CSS_LOG_DEDUP_OOM = 2,       // index=value=0; dedup allocation failed.
  XP_BOOK_CSS_LOG_PARSING = 3,         // index=file, value=0; parsing begins.
  XP_BOOK_CSS_LOG_LOW_HEAP = 4,        // index=file, value=free heap bytes.
  XP_BOOK_CSS_LOG_OVERSIZE = 5,        // index=file, value=inflated file bytes.
  XP_BOOK_CSS_LOG_CREATE = 6,          // index=file, value=0; temporary create failed.
  XP_BOOK_CSS_LOG_READ = 7,            // index=file, value=0; source read failed.
  XP_BOOK_CSS_LOG_OPEN = 8,            // index=file, value=0; temporary reopen failed.
  XP_BOOK_CSS_LOG_FAILED = 9,          // index=value=0; parse failed.
  XP_BOOK_CSS_LOG_EMPTY_PARTIAL = 10,  // index=value=0; partial parse has no rules.
  XP_BOOK_CSS_LOG_KEEP_PARTIAL = 11,   // index=value=0; previous partial cache retained.
  XP_BOOK_CSS_LOG_SAVE = 12,           // index=value=0; cache save failed.
  XP_BOOK_CSS_LOG_LOADED = 13,         // index=duplicate count, value=XpBookCssParseStatus; counts/rules are in ctx.
} XpBookCssDiagnostic;
// CLEAR runs before parsing and after cache handling. Successful OPEN_WRITE precedes STREAM,
// CLOSE, OPEN_READ, PARSE, CLOSE, REMOVE. Stream/reopen failures still REMOVE;
// DROP_TEMP follows every OPEN_WRITE attempt, including failure. Skipped files
// have no temporary owner. ctx releases the dedup table after the call.
// Required: call. Optional: diagnostic(event, index, value).
typedef struct {
  void* ctx;
  size_t (*call)(void* ctx, uint32_t operation, size_t index, size_t arg);
  void (*diagnostic)(void* ctx, uint32_t event, size_t index, size_t value);
} XpBookCssParseServices;
// Returns XpBookCssParseStatus; file indices are in [0, count). Nonzero partialCache
// means an existing partial cache is retained if this parse is also partial.
uint32_t xp_book_css_parse_files(const XpBookCssParseServices* services, size_t count, uint8_t partialCache);
typedef enum XpBookZipIndexOp {
  XP_BOOK_ZIP_INDEX_CACHE_SIZE = 0,     // arg=0; result=cached entry count.
  XP_BOOK_ZIP_INDEX_OPEN = 1,           // arg=0; retain scoped open/close owner; result=boolean.
  XP_BOOK_ZIP_INDEX_DETAILS = 2,        // arg=0; load ZIP details; result=boolean.
  XP_BOOK_ZIP_INDEX_CENTRAL = 3,        // arg=0; result=central-directory byte offset.
  XP_BOOK_ZIP_INDEX_START = 4,          // arg=start byte offset; initialize directory walker; result ignored.
  XP_BOOK_ZIP_INDEX_CLEAR = 5,          // arg=0; clear entry map (populate only); result ignored.
  XP_BOOK_ZIP_INDEX_RESERVE = 6,        // arg=0; reserve map for total entries (populate only); result ignored.
  XP_BOOK_ZIP_INDEX_CURSOR = 7,         // arg=byte offset; set valid lookup cursor; result ignored.
  XP_BOOK_ZIP_INDEX_LAST_VALID = 8,     // arg=0; result=boolean: last lookup valid.
  XP_BOOK_ZIP_INDEX_CURSOR_VALID = 9,   // arg=0; result=boolean: cursor valid.
  XP_BOOK_ZIP_INDEX_CURSOR_VALUE = 10,  // arg=0; result=cursor byte offset.
} XpBookZipIndexOp;
typedef enum XpBookZipCacheOp {
  XP_BOOK_ZIP_CACHE_FIND = 0,  // entry=null, name=target; copy cached stat to output; result=found boolean.
  XP_BOOK_ZIP_CACHE_LAST = 1,  // entry=null, name=target; copy last stat to output; result=boolean.
  XP_BOOK_ZIP_CACHE_REMEMBER =
      2,  // entry=current, name=target; copy stat/name to last lookup and output; result ignored.
  XP_BOOK_ZIP_CACHE_INSERT = 3,  // entry=current, name=entry path; copy into map; result ignored.
  XP_BOOK_ZIP_CACHE_PATHS = 4,   // entry=null, name=empty; visit cached paths; result=boolean.
  XP_BOOK_ZIP_CACHE_VISIT = 5,   // entry=current, name=entry path; invoke visitor; result ignored.
} XpBookZipCacheOp;
typedef enum XpBookZipIndexAction {
  XP_BOOK_ZIP_LOOKUP = 0,   // Lookup target using last name; result=boolean.
  XP_BOOK_ZIP_PATHS = 2,    // Enumerate paths; target/last empty; result=boolean.
  XP_BOOK_ZIP_ENTRIES = 3,  // Enumerate entries; target/last empty; result=boolean.
} XpBookZipIndexAction;
// Cache hits may bypass OPEN. Scans require OPEN then DETAILS before START/next;
// next precedes name and cache. Populate clears/reserves after START and retains
// partial results on scan failure. No close callback is issued: on every return,
// ctx releases its scoped owner, closing only a file opened by this call. DETAILS
// may use a distinct detail context under the common nesting rules.
// Required: control, next, name, cache; ctx retains the walker and scoped file owner.
typedef struct XpBookZipIndexServices {
  void* ctx;
  size_t (*control)(void* ctx, uint32_t operation, uint32_t arg);
  // next fills the entry (borrowed for this callback); false ends the walk.
  bool (*next)(void* ctx, XpZipEntry* entry);
  // name receives a current entry; bool selects target-aware reading. Null means failure.
  // Returned name bytes remain valid until the next callback; cache/visitors copy them.
  XpBookBorrow (*name)(void* ctx, const XpZipEntry* entry, bool targetAware);
  bool (*cache)(void* ctx, uint32_t operation, const XpZipEntry* entry, XpBookBorrow name);
} XpBookZipIndexServices;
uint8_t xp_book_zip_index(const XpBookZipIndexServices* services, uint32_t operation, XpBookBorrow target,
                          XpBookBorrow last);
bool xp_book_zip_populate(const XpBookZipIndexServices* services);
typedef struct {
  uint64_t hash;        // FNV-1a 64-bit hash of normalized path bytes.
  uint16_t len, index;  // Path byte length and caller's output index.
} XpBookZipTarget;
// Targets must stay sorted by (hash, len) and unchanged for the call. get returns
// an in-range target by value and may repeat indices in binary-search order.
// set receives target.index and the uncompressed size in bytes; true counts the
// match. Returns counted matches; scanning stops when all count targets are counted
// or the directory walk ends/fails. Earlier set calls are not rolled back.
size_t xp_book_zip_batch(const XpBookZipIndexServices* services, size_t count, void* ctx,
                         XpBookZipTarget (*get)(void* ctx, size_t index),
                         bool (*set)(void* ctx, uint16_t index, uint32_t uncompressedSize));
typedef enum XpBookZipDetailDiagnostic {
  XP_BOOK_ZIP_DETAIL_LOG_SMALL = 0,       // ZIP shorter than EOCD.
  XP_BOOK_ZIP_DETAIL_LOG_SIZE = 1,        // ZIP size cannot be represented.
  XP_BOOK_ZIP_DETAIL_LOG_ALLOCATION = 2,  // EOCD scan allocation failed.
  XP_BOOK_ZIP_DETAIL_LOG_SIGNATURE = 3,   // EOCD read/signature failed.
} XpBookZipDetailDiagnostic;
// cachedDetails bypasses all callbacks. Otherwise open, size, allocate and read
// run in order; save runs only after a valid EOCD. Failure stops the sequence.
// On every return, ctx releases its buffer/scoped owner; only a file opened by
// this call is closed. No release callback is issued.
// Required: open, size, allocate, read, save. Optional: diagnostic (event only).
typedef struct {
  void* ctx;
  bool (*open)(void* ctx);
  size_t (*size)(void* ctx);
  // allocate returns null on allocation failure, otherwise writable bytes owned
  // by ctx until xp_book_zip_details returns.
  uint8_t* (*allocate)(void* ctx, size_t bytes);
  bool (*read)(void* ctx, uint32_t offset, uint8_t* bytes, size_t len);
  // read returns full-read success; save copies the Details borrowed for this callback.
  void (*save)(void* ctx, const XpZipDetails* details);
  void (*diagnostic)(void* ctx, uint32_t event);
} XpBookZipDetailServices;
bool xp_book_zip_details(const XpBookZipDetailServices* services, bool cachedDetails);
void xp_book_zip_close(uint32_t* cursor, bool* valid, bool* last);
// get visits indices in order until a match; returned paths live until the next
// callback. No pointer is retained and no cleanup callback is issued.
int32_t xp_book_find_spine(XpBookBorrow target, size_t count, bool filename, void* ctx,
                           XpBookBorrow (*get)(void* ctx, size_t index));
typedef struct {
  uint16_t width, height;
  uint8_t depth;
} XpBookXtcInfo;
typedef enum XpBookXtcOp {
  XP_BOOK_XTC_EXISTS = 0,          // result=boolean: target BMP exists.
  XP_BOOK_XTC_READY = 1,           // result=boolean: book/parser loaded.
  XP_BOOK_XTC_PAGES = 2,           // result=page count.
  XP_BOOK_XTC_SETUP = 3,           // Set up cache directories; result ignored.
  XP_BOOK_XTC_FIRST = 4,           // Retain first page info; result=boolean.
  XP_BOOK_XTC_GENERATE_COVER = 5,  // Generate cover for unscaled thumbnail; result=boolean.
  XP_BOOK_XTC_OPEN_SOURCE = 6,     // Open cover copy source; result=boolean.
  XP_BOOK_XTC_OPEN_TARGET = 7,     // Open thumbnail copy target; result=boolean.
  XP_BOOK_XTC_COPY = 8,            // Copy cover bytes; result=boolean.
  XP_BOOK_XTC_LOAD = 9,            // Load first page into prepared bitmap; result=boolean.
  XP_BOOK_XTC_OPEN_IMAGE = 10,     // Open output BMP; result=boolean.
  XP_BOOK_XTC_RENDER = 11,         // Render prepared image; result ignored.
} XpBookXtcOp;
typedef enum XpBookXtcDiagnostic {
  XP_BOOK_XTC_LOG_COVER_UNLOADED = 0,  // bytes=0; cover book unloaded.
  XP_BOOK_XTC_LOG_THUMB_UNLOADED = 1,  // bytes=0; thumbnail book unloaded.
  XP_BOOK_XTC_LOG_NO_PAGES = 2,        // bytes=0; no pages.
  XP_BOOK_XTC_LOG_PAGE_INFO = 3,       // bytes=0; first-page info failed.
  XP_BOOK_XTC_LOG_COPIED = 4,          // bytes=0; unscaled copy attempted.
  XP_BOOK_XTC_LOG_COVER_OOM = 5,       // bytes=bitmap+row request; cover allocation failed.
  XP_BOOK_XTC_LOG_THUMB_OOM = 6,       // bytes=bitmap+row request; thumbnail allocation failed.
  XP_BOOK_XTC_LOG_COVER_READ = 7,      // bytes=0; cover page read failed.
  XP_BOOK_XTC_LOG_THUMB_READ = 8,      // bytes=0; thumbnail page read failed.
  XP_BOOK_XTC_LOG_COVER_OPEN = 9,      // bytes=0; cover output open failed.
  XP_BOOK_XTC_LOG_THUMB_OPEN = 10,     // bytes=0; thumbnail output open failed.
  XP_BOOK_XTC_LOG_COVER_DONE = 11,     // bytes=0; cover rendered; path in ctx.
  XP_BOOK_XTC_LOG_THUMB_DONE = 12,     // bytes=0; thumbnail rendered; dimensions/path in ctx.
} XpBookXtcDiagnostic;
// FIRST precedes info. The render path requires prepare, LOAD and OPEN_IMAGE
// success before RENDER. prepare returns false on allocation failure. The unscaled
// thumbnail path requires GENERATE_COVER, OPEN_SOURCE and OPEN_TARGET success before COPY;
// GENERATE_COVER may nest generation with a distinct context. On every return,
// ctx releases buffers/files; no release callback or output-file rollback is issued.
// Required: call, info, prepare. Optional: diagnostic(event, bytes).
typedef struct {
  void* ctx;
  size_t (*call)(void* ctx, uint32_t operation);
  // info returns retained first-page dimensions/depth by value.
  XpBookXtcInfo (*info)(void* ctx);
  // prepare(bitmap bytes, row bytes, nullable thumbnail plan) returns allocation success.
  // Copy the plan during this callback; ctx owns all buffers/files until generate returns.
  bool (*prepare)(void* ctx, size_t bitmapBytes, size_t rowBytes, const XpBookXtcThumbnail* plan);
  void (*diagnostic)(void* ctx, uint32_t event, size_t bytes);
} XpBookXtcCoverServices;
// thumbnail selects thumbnail generation; height is its requested height in pixels
// and is ignored for a cover.
bool xp_book_xtc_generate(const XpBookXtcCoverServices* services, bool thumbnail, int32_t height);
typedef enum XpBookDirectoryOp {
  XP_BOOK_DIRECTORY_EXISTS = 0,  // path=full path; result=exists boolean.
  XP_BOOK_DIRECTORY_CREATE = 1,  // path=prefix or full path; result=mkdir boolean (ignored).
} XpBookDirectoryOp;
// Required control callback borrows each path only for the callback.
// EXISTS runs first; if absent, CREATE visits prefixes then the full path. Failed
// creates do not stop later calls; there is no rollback or cleanup callback.
void xp_book_directories(XpBookBorrow path, void* ctx,
                         bool (*control)(void* ctx, uint32_t operation, XpBookBorrow path));
XpBookBorrow xp_book_txt_title(XpBookBorrow path);
typedef enum XpBookPathValue {
  XP_BOOK_PATH_HTML_DIRECTORY = 0,      // /html
  XP_BOOK_PATH_SECTIONS_DIRECTORY = 1,  // /sections
  XP_BOOK_PATH_HTML_DOCUMENT = 2,       // /html/0.html
  XP_BOOK_PATH_TEXT_HREF = 3,           // content.html
  XP_BOOK_PATH_COVER = 4,               // /cover.bmp
  XP_BOOK_PATH_THUMB_PATTERN = 5,       // /thumb_[HEIGHT].bmp
  XP_BOOK_PATH_THUMB_PREFIX = 6,        // /thumb_
} XpBookPathValue;
#ifdef __cplusplus
enum class XpBookPath : uint32_t {
  HtmlDirectory = XP_BOOK_PATH_HTML_DIRECTORY,
  SectionsDirectory = XP_BOOK_PATH_SECTIONS_DIRECTORY,
  HtmlDocument = XP_BOOK_PATH_HTML_DOCUMENT,
  TextHref = XP_BOOK_PATH_TEXT_HREF,
  Cover = XP_BOOK_PATH_COVER,
  ThumbPattern = XP_BOOK_PATH_THUMB_PATTERN,
  ThumbPrefix = XP_BOOK_PATH_THUMB_PREFIX,
};
#else
typedef uint32_t XpBookPath;
#endif
// Returns static NUL-terminated text (empty for an unknown value).
const char* xp_book_path(XpBookPath kind);
typedef enum XpBookTxtMetadataOp {
  XP_BOOK_TXT_META_TITLE = 0,     // value=empty; assign title from ctx; result ignored.
  XP_BOOK_TXT_META_LANGUAGE = 1,  // value=borrowed language; copy into metadata; result ignored.
  // value=empty; result=cover path borrowed through the following Cover callback (empty if absent).
  XP_BOOK_TXT_META_FIND_COVER = 2,
  XP_BOOK_TXT_META_COVER = 3,  // value=borrowed cover path; copy into metadata; result ignored.
} XpBookTxtMetadataOp;
// Calls TITLE, LANGUAGE, FIND_COVER, then COVER only for a nonempty result.
// The caller owns metadata/cover storage; no rollback or cleanup callback is issued.
void xp_book_txt_metadata(void* ctx, XpBookBorrow (*operation)(void* ctx, uint32_t operation, XpBookBorrow value));
typedef enum XpBookLookupDiagnostic {
  XP_BOOK_LOOKUP_NONE = 0,      // Valid index.
  XP_BOOK_LOOKUP_UNLOADED = 1,  // Cache unloaded; index=-1.
  XP_BOOK_LOOKUP_RANGE = 2,     // Index out of range; index=-1 for TOC, 0 for spine.
  XP_BOOK_LOOKUP_MISSING = 3,   // TOC spine absent; index=0.
} XpBookLookupDiagnostic;
typedef struct {
  int32_t index;
  uint32_t diagnostic;
} XpBookLookup;
XpBookLookup xp_book_lookup(bool loaded, int32_t index, int32_t count, bool toc);
// clear is required; diagnostic is optional and receives the pre-reclaim largest block in bytes.
// Below 64 KiB, clear runs before diagnostic; otherwise neither runs. The caller
// serializes the cache owner and must not recursively reclaim it from clear.
void xp_book_reclaim_content(size_t before, void (*clear)(), void (*diagnostic)(size_t before));
XpBookLookup xp_book_toc_spine(int32_t index);
XpBookBorrow xp_book_href_path(XpBookBorrow path);
#ifdef __cplusplus
}
#endif
