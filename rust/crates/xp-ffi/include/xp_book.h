#pragma once
#include <stddef.h>
#include <stdint.h>

#include "xp_book_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

// Synchronous EPUB orchestration. The context and callbacks live through the call;
// callbacks may enter other Rust modules but must not reenter this book operation.
// The C++ facade retains HAL handles, package/cache adapters and return strings.
// No state or pointer is retained by Rust and no allocation is added by this ABI.
typedef enum XpBookOperation {
  XP_BOOK_OPEN_PROTECTION = 0,
  XP_BOOK_NEW_METADATA = 1,
  XP_BOOK_NEW_CSS = 2,
  XP_BOOK_LOAD_METADATA = 3,
  XP_BOOK_IS_TEXT = 4,
  XP_BOOK_VALIDATE_TEXT = 5,
  XP_BOOK_BUILD_TEXT = 6,
  XP_BOOK_CSS_INSPECT = 7,
  XP_BOOK_CSS_LOAD = 8,
  XP_BOOK_CSS_DELETE = 9,
  XP_BOOK_COPY_METADATA = 10,
  XP_BOOK_PARSE_OPF = 11,
  XP_BOOK_DISCOVER_CSS = 12,
  XP_BOOK_RELEASE_METADATA = 13,
  XP_BOOK_PARSE_CSS = 14,
  XP_BOOK_REMOVE_SECTIONS = 15,
  XP_BOOK_CSS_CLEAR = 16,
  XP_BOOK_SETUP_CACHE = 17,
  XP_BOOK_BEGIN_WRITE = 18,
  XP_BOOK_BEGIN_OPF = 19,
  XP_BOOK_END_OPF = 20,
  XP_BOOK_BEGIN_TOC = 21,
  XP_BOOK_HAS_NAV = 22,
  XP_BOOK_PARSE_NAV = 23,
  XP_BOOK_HAS_NCX = 24,
  XP_BOOK_PARSE_NCX = 25,
  XP_BOOK_END_TOC = 26,
  XP_BOOK_END_WRITE = 27,
  XP_BOOK_BUILD_BOOK = 28,
  XP_BOOK_CLEAN_TMP = 29,
  XP_BOOK_RELEASE_WORK_METADATA = 30,
} XpBookOperation;

// Operation results: booleans are 0/1; CSS_INSPECT returns CssParser::CacheStatus,
// CSS_LOAD returns CacheLoadResult and PARSE_CSS returns ParseResult. PARSE_OPF's
// argument selects spine writes; PARSE_CSS's argument is the prior CacheStatus.
// Every other operation ignores its argument. Status enum orders are checked by
// the facade. RELEASE_WORK_METADATA destroys the temporary metadata copy.
typedef enum XpBookDiagnostic {
  XP_BOOK_LOG_LOADING = 0,
  XP_BOOK_LOG_OOM_CACHE_CSS = 1,
  XP_BOOK_LOG_TEXT_INVALID = 2,
  XP_BOOK_LOG_OOM_TEXT_CACHE = 3,
  XP_BOOK_LOG_TEXT_LOADED = 4,
  XP_BOOK_LOG_CSS_LOW_MEMORY = 5,
  XP_BOOK_LOG_CSS_REBUILD = 6,
  XP_BOOK_LOG_CSS_OPF_FAILED = 7,
  XP_BOOK_LOG_CSS_RELOAD_FAILED = 8,
  XP_BOOK_LOG_LOADED = 9,
  XP_BOOK_LOG_BUILDING = 10,
  XP_BOOK_LOG_BEGIN_WRITE_FAILED = 11,
  XP_BOOK_LOG_BEGIN_OPF_FAILED = 12,
  XP_BOOK_LOG_OPF_FAILED = 13,
  XP_BOOK_LOG_END_OPF_FAILED = 14,
  XP_BOOK_LOG_OPF_TIME = 15,
  XP_BOOK_LOG_BEGIN_TOC_FAILED = 16,
  XP_BOOK_LOG_NAV_ATTEMPT = 17,
  XP_BOOK_LOG_NCX_FALLBACK = 18,
  XP_BOOK_LOG_TOC_FAILED = 19,
  XP_BOOK_LOG_END_TOC_FAILED = 20,
  XP_BOOK_LOG_TOC_TIME = 21,
  XP_BOOK_LOG_END_WRITE_FAILED = 22,
  XP_BOOK_LOG_BUILD_FAILED = 23,
  XP_BOOK_LOG_BUILD_TIME = 24,
  XP_BOOK_LOG_TOTAL_TIME = 25,
  XP_BOOK_LOG_CLEAN_TMP_FAILED = 26,
  XP_BOOK_LOG_RELOAD_FAILED = 27,
} XpBookDiagnostic;

typedef struct XpBookServices {
  void* ctx;
  uint32_t (*operation)(void* ctx, uint32_t operation, uint32_t argument);
  uint32_t (*millis)(void* ctx);
  // Null means silent. a is elapsed milliseconds for *_TIME, otherwise zero.
  void (*diagnostic)(void* ctx, uint32_t event, uint32_t a);
} XpBookServices;

// Null services or missing required callbacks fail without side effects.
uint8_t xp_book_load(const XpBookServices* services, uint8_t build_if_missing, uint8_t skip_css);
// Arithmetic retains size_t wrapping and float evaluation order of the C++ path.
float xp_book_progress(size_t book_size, size_t previous_size, size_t current_size, float fraction);

typedef struct XpBookSpan {
  const uint8_t* data;
  size_t len;
} XpBookSpan;
typedef struct XpBookCssEntry {
  uint64_t pathHash;
  uint64_t contentKey;
  size_t cssIndex;
} XpBookCssEntry;
// entries are sorted by pathHash for observation, then by cssIndex for duplicate
// checks. The facade preserves the existing std::sort tie ordering; Rust owns
// hash collision matching and content-key selection. No allocation occurs here.
void xp_book_css_observe(XpBookCssEntry*, size_t count, XpBookSpan path, uint32_t crc, uint32_t compressed, void* ctx,
                         XpBookSpan (*path_at)(void*, size_t));
uint8_t xp_book_css_duplicate(const XpBookCssEntry*, size_t count, size_t index);
typedef enum XpBookIoControl {
  XP_BOOK_IO_NORMALIZE,
  XP_BOOK_IO_IS_TEXT,
  XP_BOOK_IO_IS_ENCRYPTED,
  XP_BOOK_IO_PLAIN_SIZE,
  XP_BOOK_IO_OPEN_EXTRACT,
  XP_BOOK_IO_FLUSH_EXTRACT,
  XP_BOOK_IO_CLOSE_EXTRACT,
  XP_BOOK_IO_REMOVE_EXTRACT
} XpBookIoControl;
typedef enum XpBookIoDiagnostic {
  XP_BOOK_IO_EMPTY_HREF,
  XP_BOOK_IO_MEMORY,
  XP_BOOK_IO_READ_FAILED,
  XP_BOOK_IO_ZIP_READ_FAILED
} XpBookIoDiagnostic;
typedef enum XpBookIoStream { XP_BOOK_IO_TEXT, XP_BOOK_IO_ZIP, XP_BOOK_IO_RESOURCE } XpBookIoStream;
typedef uint8_t (*XpBookSink)(void*, const uint8_t*, size_t);
// All callbacks are required except diagnostic. Bytes, paths, sinks and contexts
// are borrowed for one synchronous call. Returned memory uses the C allocator.
typedef struct XpBookIoServices {
  void* ctx;
  size_t (*control)(void*, uint32_t);
  uint8_t* (*zip_bytes)(void*, size_t*, uint8_t);
  uint8_t (*stream)(void*, uint32_t, size_t, uint8_t);
  uint8_t (*decrypt)(void*, XpBookSink, void*);
  size_t (*write)(void*, const uint8_t*, size_t);
  uint8_t (*size)(void*, uint8_t, size_t*);
  uint8_t* (*allocate)(size_t);
  void (*release)(uint8_t*);
  void (*diagnostic)(void*, uint32_t, size_t);
} XpBookIoServices;
uint8_t* xp_book_read_bytes(const XpBookIoServices*, uint8_t empty, size_t*, uint8_t trailing);
uint8_t xp_book_read_stream(const XpBookIoServices*, uint8_t empty, size_t chunk, uint8_t early);
uint8_t xp_book_item_size(const XpBookIoServices*, size_t*);
uint8_t xp_book_extract(const XpBookIoServices*);
typedef enum XpBookCoverOperation {
  XP_BOOK_COVER_TARGET_EXISTS = 0,
  XP_BOOK_COVER_CACHE_LOADED = 1,
  XP_BOOK_COVER_COPY_COVER = 2,
  XP_BOOK_COVER_BORROW_COVER = 3,
  XP_BOOK_COVER_IS_TEXT = 4,
  XP_BOOK_COVER_CONVERT_TEXT = 5,
  XP_BOOK_COVER_COMPANION_COVER = 6,
  XP_BOOK_COVER_NEW_SOURCE = 7,
  XP_BOOK_COVER_SOURCE_ZIP_OPEN = 8,
  XP_BOOK_COVER_PARSE_SOURCE = 9,
  XP_BOOK_COVER_RELEASE_SOURCE_ZIP = 10,
  XP_BOOK_COVER_HAS_PROTECTION = 11,
  XP_BOOK_COVER_OPEN_PROTECTION = 12,
  XP_BOOK_COVER_SETUP_CACHE = 13,
  XP_BOOK_COVER_SOURCE_COVER = 14,
  XP_BOOK_COVER_MAKE_RASTER_SOURCE = 15,
  XP_BOOK_COVER_EXTRACT_RASTER = 16,
  XP_BOOK_COVER_OPEN_SOURCE = 17,
  XP_BOOK_COVER_OPEN_TARGET = 18,
  XP_BOOK_COVER_CLOSE_SOURCE = 19,
  XP_BOOK_COVER_CLOSE_TARGET = 20,
  XP_BOOK_COVER_REMOVE_SOURCE = 21,
  XP_BOOK_COVER_REMOVE_TARGET = 22,
  XP_BOOK_COVER_CREATE_EMPTY = 23,
  XP_BOOK_COVER_PREPARE_GIF_TARGET = 24,
  XP_BOOK_COVER_LOCK = 25,
  XP_BOOK_COVER_UNLOCK = 26,
  XP_BOOK_COVER_MAKE_GIF_PATHS = 27,
  XP_BOOK_COVER_EXTRACT_GIF = 28,
  XP_BOOK_COVER_RENAME_TARGET = 29,
  XP_BOOK_COVER_EXISTS_SOURCE = 30,
  XP_BOOK_COVER_EXISTS_TARGET_TEMP = 31,
  XP_BOOK_COVER_REMOVE_TARGET_TEMP = 32,
  XP_BOOK_COVER_RELEASE_GIF_HANDLES = 33,
} XpBookCoverOperation;
typedef enum XpBookCoverDiagnostic {
  XP_BOOK_COVER_LOG_CACHE_COVER = 0,
  XP_BOOK_COVER_LOG_EMPTY_COVER = 1,
  XP_BOOK_COVER_LOG_JPEG_COVER_START = 2,
  XP_BOOK_COVER_LOG_PNG_COVER_START = 3,
  XP_BOOK_COVER_LOG_COVER_CONVERT_FAILED = 4,
  XP_BOOK_COVER_LOG_JPEG_COVER_END = 5,
  XP_BOOK_COVER_LOG_PNG_COVER_END = 6,
  XP_BOOK_COVER_LOG_UNSUPPORTED_COVER = 7,
  XP_BOOK_COVER_LOG_CACHE_THUMB = 8,
  XP_BOOK_COVER_LOG_OOM_SOURCE = 9,
  XP_BOOK_COVER_LOG_OPEN_SOURCE_METADATA = 10,
  XP_BOOK_COVER_LOG_EMPTY_THUMB = 11,
  XP_BOOK_COVER_LOG_JPEG_THUMB_START = 12,
  XP_BOOK_COVER_LOG_PNG_THUMB_START = 13,
  XP_BOOK_COVER_LOG_JPEG_THUMB_FAILED = 14,
  XP_BOOK_COVER_LOG_PNG_THUMB_FAILED = 15,
  XP_BOOK_COVER_LOG_JPEG_THUMB_END = 16,
  XP_BOOK_COVER_LOG_PNG_THUMB_END = 17,
  XP_BOOK_COVER_LOG_UNSUPPORTED_THUMB = 18,
  XP_BOOK_COVER_LOG_GIF_LOCK = 19,
  XP_BOOK_COVER_LOG_GIF_OPEN_SOURCE = 20,
  XP_BOOK_COVER_LOG_GIF_OPEN_TARGET = 21,
  XP_BOOK_COVER_LOG_GIF_RENAME = 22,
  XP_BOOK_COVER_LOG_GIF_REMOVE_TEMP = 23,
  XP_BOOK_COVER_LOG_PNG_COVER_FAILED = 24,
} XpBookCoverDiagnostic;
typedef struct XpBookCoverServices {
  void* ctx;
  uint8_t (*operation)(void*, uint32_t, uint32_t);
  XpBookSpan (*href)(void*);
  uint8_t (*convert)(void*, uint32_t, uint8_t, int32_t, int32_t);
  void (*diagnostic)(void*, uint32_t, uint32_t);
} XpBookCoverServices;
// mode: 0 cover, 1 cached thumbnail, 2 source thumbnail.
// Converter callbacks call the existing Rust-backed image adapters; no pixel data
// is retained here. Context owns all HAL handles through return, even on failure.
uint8_t xp_book_cover(const XpBookCoverServices*, uint32_t mode, int32_t height);
typedef enum XpBookGifExtractOperation {
  XP_BOOK_GIF_OPEN,
  XP_BOOK_GIF_STREAM,
  XP_BOOK_GIF_CLOSE,
  XP_BOOK_GIF_REMOVE,
  XP_BOOK_GIF_EXISTS
} XpBookGifExtractOperation;
typedef enum XpBookGifExtractDiagnostic {
  XP_BOOK_GIF_OPEN_FAILED,
  XP_BOOK_GIF_STREAM_FAILED,
  XP_BOOK_GIF_REMOVE_FAILED
} XpBookGifExtractDiagnostic;
typedef struct XpBookGifExtractServices {
  void* ctx;
  uint8_t (*operation)(void*, uint32_t);
  void (*diagnostic)(void*, uint32_t);
} XpBookGifExtractServices;
uint8_t xp_book_gif_extract(const XpBookGifExtractServices*);

typedef enum XpBookMetadataOperation {
  XP_BOOK_META_CLEAR_OUTPUT = 0,
  XP_BOOK_META_IS_TEXT = 1,
  XP_BOOK_META_TEXT_TITLE = 2,
  XP_BOOK_META_NEW_CACHE = 3,
  XP_BOOK_META_LOAD_CACHE = 4,
  XP_BOOK_META_COPY_CACHE = 5,
  XP_BOOK_META_DROP_CACHE = 6,
  XP_BOOK_META_LOAD_SOURCE = 7,
  XP_BOOK_META_OPEN_SOURCE = 8,
  XP_BOOK_META_PARSE_SOURCE = 9,
  XP_BOOK_META_CLOSE_SOURCE = 10,
  XP_BOOK_META_COPY_SOURCE = 11,
  XP_BOOK_META_FIND_OPF = 12,
  XP_BOOK_META_SIZE_OPF = 13,
  XP_BOOK_META_SETUP_OPF = 14,
  XP_BOOK_META_READ_OPF = 15,
  XP_BOOK_META_COPY_SYNC = 16,
  XP_BOOK_META_CACHE_EXISTS = 17,
  XP_BOOK_META_CACHE_REMOVE = 18,
  XP_BOOK_META_CACHE_MKDIR = 19,
} XpBookMetadataOperation;
typedef enum XpBookMetadataDiagnostic {
  XP_BOOK_META_LOG_CACHE_OOM = 0,
  XP_BOOK_META_LOG_SOURCE_OPEN = 1,
  XP_BOOK_META_LOG_SYNC_OPEN = 2,
  XP_BOOK_META_LOG_CACHE_ABSENT = 3,
  XP_BOOK_META_LOG_CACHE_CLEAR_FAILED = 4,
  XP_BOOK_META_LOG_CACHE_CLEARED = 5,
} XpBookMetadataDiagnostic;
typedef struct XpBookMetadataServices {
  void* ctx;
  uint8_t (*operation)(void*, uint32_t);
  void (*diagnostic)(void*, uint32_t);
} XpBookMetadataServices;
uint8_t xp_book_metadata(const XpBookMetadataServices*);
uint8_t xp_book_metadata_source(const XpBookMetadataServices*);
uint8_t xp_book_sync_metadata(const XpBookMetadataServices*);
uint8_t xp_book_clear_cache(const XpBookMetadataServices*);
void xp_book_setup_cache(const XpBookMetadataServices*);
// Borrowed file services. No pointer survives a call. File handles and text
// allocations remain owned by the C++ facade; diagnostics never reenter Rust.
typedef enum XpBookFileOperation {
  XP_BOOK_FILE_OPEN_READ = 0,
  XP_BOOK_FILE_SEEK = 1,
  XP_BOOK_FILE_SIZE = 2,
  XP_BOOK_FILE_POSITION = 3,
  XP_BOOK_FILE_CLOSE = 4,
  XP_BOOK_FILE_RELEASE_TEXT = 5,
  XP_BOOK_FILE_EXISTS_FINAL = 6,
  XP_BOOK_FILE_REMOVE_FINAL = 7,
  XP_BOOK_FILE_REMOVE_TEMPORARY = 8,
  XP_BOOK_FILE_PROMOTE_TEMPORARY = 9,
  XP_BOOK_FILE_EXISTS_TEMPORARY = 10,
  XP_BOOK_FILE_IS_OPEN = 11,
} XpBookFileOperation;

typedef enum XpBookSectionDiagnostic {
  XP_BOOK_SECTION_SHORT_RECORD = 0,
  XP_BOOK_SECTION_INVALID_RECORD = 1,
  XP_BOOK_SECTION_INVALID_STRING = 2,
  XP_BOOK_SECTION_VISIBLE_SEEK = 3,
  XP_BOOK_SECTION_VISIBLE_READ = 4,
  XP_BOOK_SECTION_WRITE_RECORD = 5,
  XP_BOOK_SECTION_INVALID_LUT = 6,
  XP_BOOK_SECTION_PROMOTE = 7,
  XP_BOOK_SECTION_TRUNCATED_HEADER = 8,
  XP_BOOK_SECTION_UNKNOWN_VERSION = 9,
  XP_BOOK_SECTION_PARAMETERS = 10,
  XP_BOOK_SECTION_MALFORMED_PARTIAL = 11,
  XP_BOOK_SECTION_LOADED = 12,
  XP_BOOK_SECTION_CACHE_MISSING = 13,
  XP_BOOK_SECTION_CACHE_CLEAR_FAILED = 14,
  XP_BOOK_SECTION_CACHE_CLEARED = 15,
  XP_BOOK_SECTION_PAGE_FILE_CLOSED = 16,
  XP_BOOK_SECTION_PAGE_WRITE_FAILED = 17,
  XP_BOOK_SECTION_PAGE_PROCESSED = 18,
  XP_BOOK_SECTION_HEADER_FILE_CLOSED = 19,

} XpBookSectionDiagnostic;

typedef struct XpBookFileServices {
  void* ctx;
  size_t (*operation)(void* ctx, uint32_t operation, size_t argument);
  size_t (*read)(void* ctx, uint8_t* bytes, size_t length);
  size_t (*write)(void* ctx, const uint8_t* bytes, size_t length);
  uint8_t* (*resize_text)(void* ctx, size_t length);
  void (*diagnostic)(void* ctx, uint32_t event, uint32_t a, uint32_t b);
} XpBookFileServices;

typedef enum XpBookSectionQuery {
  XP_BOOK_SECTION_PAGE_RANGE = 0,
  XP_BOOK_SECTION_PAGE_COUNT = 1,
  XP_BOOK_SECTION_ANCHOR_MAP = 2,
  XP_BOOK_SECTION_PARAGRAPH_PAGE = 3,
  XP_BOOK_SECTION_LIST_PAGE = 4,
  XP_BOOK_SECTION_PAGE_PARAGRAPH = 5,
  XP_BOOK_SECTION_PAGE_VISIBLE = 6,
  XP_BOOK_SECTION_VISIBLE_PAGE = 7,
} XpBookSectionQuery;

// output has three slots; PAGE_RANGE returns start/end/visible, others use slot 0.
uint8_t xp_book_section_query(const XpBookFileServices* services, uint32_t query, uint32_t index, uint8_t prefer_first,
                              uint32_t output[3]);
uint8_t xp_book_section_anchor(const XpBookFileServices* services, const uint8_t* needle, size_t length,
                               uint32_t* output);

typedef struct XpBookSectionEstimate {
  uint32_t consumed, total, partial_consumed, partial_total, smoothed_at;
  float smoothed;
  uint16_t pages, built_pages, partial_pages;
  uint8_t partial, building;
} XpBookSectionEstimate;
uint16_t xp_book_section_estimate(XpBookSectionEstimate* state);

typedef enum XpBookSectionOperation {
  XP_BOOK_SECTION_OP_HAS_BUILD = 0,
  XP_BOOK_SECTION_OP_HAS_PARSER = 1,
  XP_BOOK_SECTION_OP_BUILT_PAGES = 2,
  XP_BOOK_SECTION_OP_PARTIAL = 3,
  XP_BOOK_SECTION_OP_PARTIAL_PAGES = 4,
  XP_BOOK_SECTION_OP_TOTAL = 5,
  XP_BOOK_SECTION_OP_REUSED_HTML = 6,
  XP_BOOK_SECTION_OP_PARSER_STEP = 7,
  XP_BOOK_SECTION_OP_PARSER_FINISH = 8,
  XP_BOOK_SECTION_OP_PARSER_ABORT = 9,
  XP_BOOK_SECTION_OP_PARSER_CONSUMED = 10,
  XP_BOOK_SECTION_OP_YIELD = 11,
  XP_BOOK_SECTION_OP_CSS_CLEAR = 12,
  XP_BOOK_SECTION_OP_RESET_BUILD = 13,
  XP_BOOK_SECTION_OP_FILE_OPEN = 14,
  XP_BOOK_SECTION_OP_FILE_CLOSE = 15,
  XP_BOOK_SECTION_OP_TMP_BIN_REMOVE = 16,
  XP_BOOK_SECTION_OP_FINAL_EXISTS = 17,
  XP_BOOK_SECTION_OP_FINAL_REMOVE = 18,
  XP_BOOK_SECTION_OP_TMP_HTML_EXISTS = 19,
  XP_BOOK_SECTION_OP_TMP_HTML_REMOVE = 20,
  XP_BOOK_SECTION_OP_PROMOTE_HTML = 21,
  XP_BOOK_SECTION_OP_COMMIT_FINAL = 22,
  XP_BOOK_SECTION_OP_COMMIT_PARTIAL = 23,
  XP_BOOK_SECTION_OP_SET_COMPLETE = 24,
  XP_BOOK_SECTION_OP_SET_PARTIAL = 25,
  XP_BOOK_SECTION_OP_SET_PARTIAL_PAGES = 26,
  XP_BOOK_SECTION_OP_SET_PAGES = 27,
  XP_BOOK_SECTION_OP_SET_BUILT_PAGES = 28,
  XP_BOOK_SECTION_OP_SET_PARTIAL_CONSUMED = 29,
  XP_BOOK_SECTION_OP_SET_PARTIAL_TOTAL = 30,
  XP_BOOK_SECTION_OP_SET_CONSUMED = 31,
} XpBookSectionOperation;

typedef enum XpBookSectionEvent {
  XP_BOOK_SECTION_EVENT_NO_BUILD = 0,
  XP_BOOK_SECTION_EVENT_PARSE_ERROR = 1,
  XP_BOOK_SECTION_EVENT_FINALIZE_ERROR = 2,
  XP_BOOK_SECTION_EVENT_HTML_PROMOTION = 3,
  XP_BOOK_SECTION_EVENT_SUSPENDED = 4,
} XpBookSectionEvent;

typedef enum XpBookSectionTransition {
  XP_BOOK_SECTION_ADVANCE = 0,
  XP_BOOK_SECTION_FINISH = 1,
  XP_BOOK_SECTION_SUSPEND = 2,
  XP_BOOK_SECTION_ABANDON = 3,
} XpBookSectionTransition;
uint8_t xp_book_section_transition(const XpBookServices* services, uint32_t transition, int32_t argument);

typedef struct XpBookAnchor {
  const uint8_t* text;
  size_t length;
  uint16_t page;
} XpBookAnchor;
typedef struct XpBookBuildRecords {
  void* ctx;
  size_t entries, anchors;
  uint32_t (*entry)(void* ctx, size_t index, uint32_t field);
  void (*anchor)(void* ctx, size_t index, XpBookAnchor* out);
} XpBookBuildRecords;
uint8_t xp_book_section_commit(const XpBookFileServices* services, const XpBookBuildRecords* records, uint8_t version,
                               uint16_t pages, uint32_t consumed, uint32_t total);

typedef enum XpBookLutField {
  XP_BOOK_LUT_FILE = 0,
  XP_BOOK_LUT_PARAGRAPH = 1,
  XP_BOOK_LUT_LIST = 2,
  XP_BOOK_LUT_VISIBLE = 3,
} XpBookLutField;

typedef enum XpBookSectionStartOperation {
  XP_BOOK_START_HAS_BUILD = 0,
  XP_BOOK_START_RELEASE_FONTS = 1,
  XP_BOOK_START_SET_COMPLETE = 2,
  XP_BOOK_START_SET_BUILT_PAGES = 3,
  XP_BOOK_START_PARTIAL = 4,
  XP_BOOK_START_PARTIAL_PAGES = 5,
  XP_BOOK_START_SET_PAGES = 6,
  XP_BOOK_START_TMP_BIN_EXISTS = 7,
  XP_BOOK_START_TMP_BIN_REMOVE = 8,
  XP_BOOK_START_PREPARE_PATHS = 9,
  XP_BOOK_START_MKDIR_SECTIONS = 10,
  XP_BOOK_START_HTML_EXISTS = 11,
  XP_BOOK_START_MKDIR_HTML = 12,
  XP_BOOK_START_DELAY = 13,
  XP_BOOK_START_TMP_HTML_EXISTS = 14,
  XP_BOOK_START_TMP_HTML_REMOVE = 15,
  XP_BOOK_START_TMP_HTML_OPEN = 16,
  XP_BOOK_START_STREAM = 17,
  XP_BOOK_START_TMP_HTML_SIZE = 18,
  XP_BOOK_START_TMP_HTML_CLOSE = 19,
  XP_BOOK_START_TMP_HTML_DROP = 20,
  XP_BOOK_START_PROMOTE_HTML = 21,
  XP_BOOK_START_BIN_OPEN = 22,
  XP_BOOK_START_WRITE_HEADER = 23,
  XP_BOOK_START_ALLOCATE_CONTEXT = 24,
  XP_BOOK_START_CONFIGURE_CONTEXT = 25,
  XP_BOOK_START_EMBEDDED_STYLE = 26,
  XP_BOOK_START_GET_CSS = 27,
  XP_BOOK_START_LOAD_CSS = 28,
  XP_BOOK_START_CLEAR_CSS = 29,
  XP_BOOK_START_FILE_CLOSE = 30,
  XP_BOOK_START_CONTEXT_REUSED = 31,
  XP_BOOK_START_TOC_FIRST = 32,
  XP_BOOK_START_TOC_COUNT = 33,
  XP_BOOK_START_TOC_READ = 34,
  XP_BOOK_START_TOC_SPINE = 35,
  XP_BOOK_START_SPINE = 36,
  XP_BOOK_START_TOC_LENGTH = 37,
  XP_BOOK_START_TOC_MOVE = 38,
  XP_BOOK_START_TOC_DROP = 39,
  XP_BOOK_START_ALLOCATE_PARSER = 40,
  XP_BOOK_START_CONFIGURE_PARSER = 41,
  XP_BOOK_START_ACTIVATE = 42,
  XP_BOOK_START_BEGIN_PARSE = 43,
  XP_BOOK_START_ABANDON = 44,
  XP_BOOK_START_SET_TOTAL = 45,
} XpBookSectionStartOperation;
typedef enum XpBookSectionStartEvent {
  XP_BOOK_START_EVENT_ACTIVE = 0,
  XP_BOOK_START_EVENT_REUSED = 1,
  XP_BOOK_START_EVENT_RETRY = 2,
  XP_BOOK_START_EVENT_REMOVED_INCOMPLETE = 3,
  XP_BOOK_START_EVENT_STREAM_FAILED = 4,
  XP_BOOK_START_EVENT_STREAMED = 5,
  XP_BOOK_START_EVENT_PROMOTION = 6,
  XP_BOOK_START_EVENT_CONTEXT_OOM = 7,
  XP_BOOK_START_EVENT_CSS_LOW = 8,
  XP_BOOK_START_EVENT_CSS_INVALID = 9,
  XP_BOOK_START_EVENT_PARSER_OOM = 10,
  XP_BOOK_START_EVENT_BEGIN_FAILED = 11,
} XpBookSectionStartEvent;
uint8_t xp_book_section_start(const XpBookServices* services);

typedef struct XpBookSectionLoaded {
  uint32_t consumed, total;
  uint16_t pages, partial_pages;
  uint8_t partial;
} XpBookSectionLoaded;
uint8_t xp_book_section_load(const XpBookFileServices* io, const uint32_t expected[20], XpBookSectionLoaded* state);
uint8_t xp_book_section_clear(const XpBookFileServices* io);
uint32_t xp_book_section_append(const XpBookFileServices* io, const uint8_t* bytes, size_t length, uint16_t* built,
                                uint16_t* pages);
void xp_book_section_header(const XpBookFileServices* io, const uint32_t values[20]);

const char* xp_book_entity(const uint8_t* input, size_t length);
uint32_t xp_book_layout_signature(const uint32_t fields[12], float compression);
uint8_t xp_book_link_contains(const int16_t bounds[4], int32_t x, int32_t y, int32_t slop, int32_t minimum);
uint8_t xp_book_tag_equals(const uint8_t* name, size_t length, const uint8_t* tag, size_t tag_length);
uint8_t xp_book_tag_hidden(const uint8_t* name, size_t length);
// 0 accepted, 1 nonpositive, 2 dimension limit, 3 pixel limit.
uint8_t xp_book_image_dimensions(int64_t width, int64_t height);
uint8_t xp_book_image_yield(uint32_t* last, uint32_t now);

typedef enum XpBookMetadataOperation {
  XP_BOOK_META_RESET_WRITER = 0,
  XP_BOOK_META_OPEN = 1,
  XP_BOOK_META_READ_VERSION = 2,
  XP_BOOK_META_CLOSE = 3,
  XP_BOOK_META_READ_LUT = 4,
  XP_BOOK_META_READ_SPINE_COUNT = 5,
  XP_BOOK_META_READ_TOC_COUNT = 6,
  XP_BOOK_META_READ_METADATA = 7,
  XP_BOOK_META_RESET_WINDOW = 8,
  XP_BOOK_META_RESERVE_WINDOW = 9,
  XP_BOOK_META_SEEK_ENTRIES = 10,
  XP_BOOK_META_READ_CUMULATIVE = 11,
  XP_BOOK_META_ADD_WINDOW_START = 12,
  XP_BOOK_META_SET_LOADED = 13,
  XP_BOOK_META_SPINE_COUNT = 14,
  XP_BOOK_META_TOC_COUNT = 15,
  XP_BOOK_META_BOOK_SIZE = 16,
  XP_BOOK_META_LOADED = 17,
  XP_BOOK_META_WINDOW_FIRST = 18,
  XP_BOOK_META_SET_WINDOW_FIRST = 19,
  XP_BOOK_META_WINDOW_START = 20,
  XP_BOOK_META_WINDOW_VALUE = 21,
  XP_BOOK_META_WINDOW_COUNT = 22,
  XP_BOOK_META_LOCK = 23,
  XP_BOOK_META_FILL_OPEN = 24,
  XP_BOOK_META_FILL_SEEK_LUT = 25,
  XP_BOOK_META_FILL_READ_POSITION = 26,
  XP_BOOK_META_FILL_SEEK_POSITION = 27,
  XP_BOOK_META_FILL_READ_HREF = 28,
  XP_BOOK_META_FILL_SKIP_HREF = 29,
  XP_BOOK_META_FILL_READ_WINDOW = 30,
  XP_BOOK_META_FILL_SKIP_TOC = 31,
  XP_BOOK_META_FILL_DROP = 32,
  XP_BOOK_META_ENTRY_SEEK = 33,
  XP_BOOK_META_ENTRY_POSITION = 34,
  XP_BOOK_META_PREPARE_TEMPORARY = 35,
  XP_BOOK_META_TEMPORARY_EXISTS = 36,
  XP_BOOK_META_TEMPORARY_REMOVE = 37,
  XP_BOOK_META_DROP_TEMPORARY = 38,
} XpBookMetadataOperation;
uint8_t xp_book_metadata_load(const XpBookServices* services);
uint32_t xp_book_metadata_cumulative(const XpBookServices* services, int32_t index);
int32_t xp_book_metadata_spine(const XpBookServices* services, uint32_t size);

typedef enum XpBookCssOperation {
  XP_BOOK_CSS_IO_EMPTY_PATH = 0,
  XP_BOOK_CSS_IO_PREPARE_PATHS = 1,
  XP_BOOK_CSS_IO_EXISTS_FINAL = 2,
  XP_BOOK_CSS_IO_EXISTS_BACKUP = 3,
  XP_BOOK_CSS_IO_RENAME_BACKUP_FINAL = 4,
  XP_BOOK_CSS_IO_REMOVE_FINAL = 5,
  XP_BOOK_CSS_IO_REMOVE_TEMPORARY = 6,
  XP_BOOK_CSS_IO_REMOVE_BACKUP = 7,
  XP_BOOK_CSS_IO_RENAME_FINAL_BACKUP = 8,
  XP_BOOK_CSS_IO_RENAME_TEMPORARY_FINAL = 9,
  XP_BOOK_CSS_IO_OPEN_FINAL = 10,
  XP_BOOK_CSS_IO_OPEN_TEMPORARY = 11,
  XP_BOOK_CSS_IO_CLOSE = 12,
  XP_BOOK_CSS_IO_READ_WORD = 13,
  XP_BOOK_CSS_IO_WORD = 14,
  XP_BOOK_CSS_IO_AVAILABLE = 15,
  XP_BOOK_CSS_IO_SKIP = 16,
  XP_BOOK_CSS_IO_ENSURE = 17,
  XP_BOOK_CSS_IO_CLEAR = 18,
  XP_BOOK_CSS_IO_ALLOCATE_BUFFER = 19,
  XP_BOOK_CSS_IO_READ_BUFFER = 20,
  XP_BOOK_CSS_IO_FIRST_BYTE = 21,
  XP_BOOK_CSS_IO_READ_CACHE_VERSION = 22,
  XP_BOOK_CSS_IO_READ_CACHE_BUFFER = 23,
  XP_BOOK_CSS_IO_READ_CACHE_END = 24,
  XP_BOOK_CSS_IO_RULE_COUNT = 25,
  XP_BOOK_CSS_IO_HAS_HANDLE = 26,
  XP_BOOK_CSS_IO_ALLOCATE_ORDER = 27,
  XP_BOOK_CSS_IO_PREPARE_ORDER = 28,
  XP_BOOK_CSS_IO_WRITE_NEXT = 29,
  XP_BOOK_CSS_IO_WRITTEN = 30,
  XP_BOOK_CSS_IO_WRITE_BUFFER = 31,
  XP_BOOK_CSS_IO_WRITE_WORD = 32,
  XP_BOOK_CSS_IO_DROP_ORDER = 33,
  XP_BOOK_CSS_IO_SOURCE_VALID = 34,
  XP_BOOK_CSS_IO_FEED_BUFFER = 35,
  XP_BOOK_CSS_IO_FEED_END = 36,
  XP_BOOK_CSS_IO_RESTORE = 37,
} XpBookCssOperation;
typedef enum XpBookCssEvent {
  XP_BOOK_CSS_IO_EVENT_RESTORE_FAILED = 0,
  XP_BOOK_CSS_IO_EVENT_RESTORED = 1,
  XP_BOOK_CSS_IO_EVENT_OUTPUT_OOM = 2,
  XP_BOOK_CSS_IO_EVENT_ORDER_OOM = 3,
  XP_BOOK_CSS_IO_EVENT_ORDER_FAILED = 4,
  XP_BOOK_CSS_IO_EVENT_WRITE_FAILED = 5,
  XP_BOOK_CSS_IO_EVENT_BACKUP_FAILED = 6,
  XP_BOOK_CSS_IO_EVENT_PROMOTE_FAILED = 7,
  XP_BOOK_CSS_IO_EVENT_ROLLBACK_FAILED = 8,
  XP_BOOK_CSS_IO_EVENT_SAVED_COMPLETE = 9,
  XP_BOOK_CSS_IO_EVENT_SAVED_PARTIAL = 10,
  XP_BOOK_CSS_IO_EVENT_VERSION = 11,
  XP_BOOK_CSS_IO_EVENT_INPUT_OOM = 12,
  XP_BOOK_CSS_IO_EVENT_LOADED_COMPLETE = 13,
  XP_BOOK_CSS_IO_EVENT_LOADED_PARTIAL = 14,
  XP_BOOK_CSS_IO_EVENT_INVALID_SOURCE = 15,
  XP_BOOK_CSS_IO_EVENT_SOURCE_OOM = 16,
  XP_BOOK_CSS_IO_EVENT_PARSED = 17,
} XpBookCssEvent;
uint32_t xp_book_css(const XpBookServices* services, uint32_t action, uint8_t complete);

typedef enum XpBookTocOperation {
  XP_BOOK_TOC_FIRST = 0,
  XP_BOOK_TOC_COUNT = 1,
  XP_BOOK_TOC_ALLOCATE = 2,
  XP_BOOK_TOC_OPEN_ANCHORS = 3,
  XP_BOOK_TOC_BUILD_ANCHORS = 4,
  XP_BOOK_TOC_SELECT_BUILD = 5,
  XP_BOOK_TOC_SELECT_DISK = 6,
  XP_BOOK_TOC_ACTIVE_HASH = 7,
  XP_BOOK_TOC_TOC_MATCHES = 8,
  XP_BOOK_TOC_BUILD_OVERRIDE = 9,
  XP_BOOK_TOC_REWIND_DISK = 10,
  XP_BOOK_TOC_READ_TOC = 11,
  XP_BOOK_TOC_TOC_SPINE = 12,
  XP_BOOK_TOC_SPINE = 13,
  XP_BOOK_TOC_TOC_LENGTH = 14,
  XP_BOOK_TOC_TOC_HASH = 15,
  XP_BOOK_TOC_DROP_TOC = 16,
} XpBookTocOperation;
typedef struct XpBookTocTarget {
  size_t hash;
  int32_t index;
} XpBookTocTarget;
typedef struct XpBookTocServices {
  void* ctx;
  size_t (*operation)(void*, uint32_t operation, size_t argument);
} XpBookTocServices;
int32_t xp_book_section_toc(const XpBookTocServices* services, int32_t page);

uint8_t xp_book_section_build_anchor(const XpBookBuildRecords* records, const uint8_t* needle, size_t length,
                                     uint32_t* page);
uint8_t xp_book_section_find_anchor(const XpBookFileServices* io, const XpBookBuildRecords* records,
                                    const uint8_t* needle, size_t length, uint32_t* page);
uint8_t xp_book_section_visible(const XpBookFileServices* io, const XpBookBuildRecords* records, uint32_t offset,
                                uint8_t first, uint32_t* page);
uint8_t xp_book_section_page_visible(const XpBookFileServices* io, const XpBookBuildRecords* records, uint16_t page,
                                     uint32_t* offset);
uint8_t xp_book_section_reached(const XpBookBuildRecords* records, uint32_t offset);
uint8_t xp_book_section_page_source(int32_t page, size_t entries, uint16_t pages, uint16_t partial_pages,
                                    uint8_t building, uint8_t partial);
typedef struct XpBookPageLoader {
  void* ctx;
  void* (*load)(void* ctx, uint32_t bytes, uint32_t visible);
} XpBookPageLoader;
void* xp_book_section_built_page(const XpBookFileServices* io, const XpBookBuildRecords* records, int32_t page,
                                 const XpBookPageLoader* loader);

typedef struct XpBookTextSpan {
  const uint8_t* data;
  size_t length;
} XpBookTextSpan;
typedef struct XpBookPageText {
  void* ctx;
  uint16_t elements;
  uint32_t (*field)(void* ctx, uint16_t element, uint32_t field);
  XpBookTextSpan (*word)(void* ctx, uint16_t element, uint16_t word);
  uint8_t* (*resize)(void* ctx, size_t length);
} XpBookPageText;
uint8_t xp_book_section_text(const XpBookPageText* page);

typedef enum XpBookParseOperation {
  XP_BOOK_PARSE_ENSURE = 0,
  XP_BOOK_PARSE_BEGIN_ASSEMBLY = 1,
  XP_BOOK_PARSE_OOM = 2,
  XP_BOOK_PARSE_ALLOCATE_BUFFER = 3,
  XP_BOOK_PARSE_SET_CAPACITY = 4,
  XP_BOOK_PARSE_OPEN = 5,
  XP_BOOK_PARSE_HAS_POPUP = 6,
  XP_BOOK_PARSE_SIZE = 7,
  XP_BOOK_PARSE_POPUP = 8,
  XP_BOOK_PARSE_START_CLOCK = 9,
  XP_BOOK_PARSE_STOPPED = 10,
  XP_BOOK_PARSE_READ = 11,
  XP_BOOK_PARSE_AVAILABLE = 12,
  XP_BOOK_PARSE_FEED = 13,
  XP_BOOK_PARSE_FEED_ERROR = 14,
  XP_BOOK_PARSE_DESTROY = 15,
  XP_BOOK_PARSE_RELEASE_BUFFER = 16,
  XP_BOOK_PARSE_IS_OPEN = 17,
  XP_BOOK_PARSE_CLOSE = 18,
  XP_BOOK_PARSE_HAS_CHAPTER = 19,
  XP_BOOK_PARSE_FINISH_ASSEMBLY = 20,
} XpBookParseOperation;
typedef enum XpBookParseEvent {
  XP_BOOK_PARSE_EVENT_PARSER_OOM = 0,
  XP_BOOK_PARSE_EVENT_BUFFER_OOM = 1,
  XP_BOOK_PARSE_EVENT_LAYOUT_OOM = 2,
  XP_BOOK_PARSE_EVENT_READ_ERROR = 3,
  XP_BOOK_PARSE_EVENT_PARSE_ERROR = 4,
  XP_BOOK_PARSE_EVENT_TIME = 5,
  XP_BOOK_PARSE_EVENT_LAYOUT_AFTER_FEED = 6,
  XP_BOOK_PARSE_EVENT_LAYOUT_BEFORE_FINISH = 7,
  XP_BOOK_PARSE_EVENT_LAYOUT_AFTER_FINISH = 8,
} XpBookParseEvent;
uint32_t xp_book_parse(const XpBookServices* services, uint32_t action);

typedef struct XpBookMutableSpan {
  uint8_t* data;
  size_t length;
} XpBookMutableSpan;
typedef struct XpBookExtensionServices {
  void* ctx;
  XpBookMutableSpan (*view)(void* ctx);
  void (*suffix)(void* ctx, size_t offset);
} XpBookExtensionServices;
uint8_t xp_book_image_decoder(const XpBookExtensionServices* services);
void xp_book_image_placeholder(int32_t x, int32_t y, int32_t width, int32_t height, void* ctx,
                               void (*rectangle)(void*, int32_t, int32_t, int32_t, int32_t, uint8_t));

typedef enum XpBookProbeOperation {
  XP_BOOK_PROBE_RESOLVE = 0,
  XP_BOOK_PROBE_SUPPORTED = 1,
  XP_BOOK_PROBE_PREPARE_CACHE = 2,
  XP_BOOK_PROBE_PROBE = 3,
  XP_BOOK_PROBE_RETRY_PROBE = 4,
  XP_BOOK_PROBE_HAS_POPUP = 5,
  XP_BOOK_PROBE_POPUP_SHOWN = 6,
  XP_BOOK_PROBE_SET_POPUP = 7,
  XP_BOOK_PROBE_POPUP = 8,
  XP_BOOK_PROBE_OPEN = 9,
  XP_BOOK_PROBE_EXTRACT = 10,
  XP_BOOK_PROBE_FLUSH = 11,
  XP_BOOK_PROBE_CLOSE = 12,
  XP_BOOK_PROBE_DECODER = 13,
  XP_BOOK_PROBE_DIMENSIONS = 14,
  XP_BOOK_PROBE_DELAY = 15,
  XP_BOOK_PROBE_STORE_SIZE = 16,
  XP_BOOK_PROBE_ACCEPT = 17,
  XP_BOOK_PROBE_REMOVE = 18,
} XpBookProbeOperation;
typedef enum XpBookProbeEvent {
  XP_BOOK_PROBE_EVENT_FOUND = 0,
  XP_BOOK_PROBE_EVENT_EXTRACT_FAILED = 1,
  XP_BOOK_PROBE_EVENT_DIMENSIONS = 2,
  XP_BOOK_PROBE_EVENT_DIMENSIONS_FAILED = 3,
} XpBookProbeEvent;
uint8_t xp_book_probe_image(const XpBookServices* services);

typedef enum XpBookJpegOperation {
  XP_BOOK_JPEG_HEAP = 0,
  XP_BOOK_JPEG_ALLOCATE = 1,
  XP_BOOK_JPEG_OPEN = 2,
  XP_BOOK_JPEG_VALIDATE = 3,
  XP_BOOK_JPEG_PROGRESSIVE = 4,
  XP_BOOK_JPEG_DRAWING = 5,
  XP_BOOK_JPEG_PIPELINE_INIT = 6,
  XP_BOOK_JPEG_PREPARE = 7,
  XP_BOOK_JPEG_DENOMINATOR = 8,
  XP_BOOK_JPEG_CONFIGURE = 9,
  XP_BOOK_JPEG_HAS_CACHE = 10,
  XP_BOOK_JPEG_BEGIN_CACHE = 11,
  XP_BOOK_JPEG_BEGIN_TIME = 12,
  XP_BOOK_JPEG_DECODE = 13,
  XP_BOOK_JPEG_END_TIME = 14,
  XP_BOOK_JPEG_FINISH = 15,
  XP_BOOK_JPEG_SUCCESS = 16,
} XpBookJpegOperation;
typedef enum XpBookJpegEvent {
  XP_BOOK_JPEG_EVENT_HEAP = 0,
  XP_BOOK_JPEG_EVENT_DIMENSION_ALLOCATION = 1,
  XP_BOOK_JPEG_EVENT_DIMENSION_OPEN = 2,
  XP_BOOK_JPEG_EVENT_DIMENSIONS = 3,
  XP_BOOK_JPEG_EVENT_RENDER = 4,
  XP_BOOK_JPEG_EVENT_ALLOCATION = 5,
  XP_BOOK_JPEG_EVENT_OPEN = 6,
  XP_BOOK_JPEG_EVENT_PROGRESSIVE = 7,
  XP_BOOK_JPEG_EVENT_CACHE = 8,
  XP_BOOK_JPEG_EVENT_DECODE = 9,
  XP_BOOK_JPEG_EVENT_TIME = 10,
  XP_BOOK_JPEG_EVENT_DIMENSION_HEAP = 11,
} XpBookJpegEvent;
uint8_t xp_book_jpeg(const XpBookServices* services, uint8_t render);

size_t xp_book_page_allocation(size_t header, size_t arena, size_t alignment, size_t elements, size_t element_bytes);
uint8_t xp_book_page_images(const uint8_t* arena, size_t length, void* ctx, uint8_t (*needs_decode)(void*, uint16_t));

typedef enum XpBookSizeOperation {
  XP_BOOK_SIZE_ZIP = 0,
  XP_BOOK_SIZE_RAW_SIZE = 1,
  XP_BOOK_SIZE_OPEN_ZIP = 2,
  XP_BOOK_SIZE_COUNT = 3,
  XP_BOOK_SIZE_ALLOCATE_SIZES = 4,
  XP_BOOK_SIZE_REWIND = 5,
  XP_BOOK_SIZE_APPEND = 6,
  XP_BOOK_SIZE_TARGETS = 7,
  XP_BOOK_SIZE_SORT = 8,
  XP_BOOK_SIZE_FILL = 9,
  XP_BOOK_SIZE_CLEAR = 10,
} XpBookSizeOperation;
typedef enum XpBookSizeEvent {
  XP_BOOK_SIZE_EVENT_OPEN_FAILED = 0,
  XP_BOOK_SIZE_EVENT_BATCH = 1,
  XP_BOOK_SIZE_EVENT_MATCHED = 2,
} XpBookSizeEvent;
uint8_t xp_book_sizes(const XpBookServices* services);
uint8_t xp_book_size_less(uint64_t a_hash, uint16_t a_length, uint64_t b_hash, uint16_t b_length);

uint8_t xp_book_metadata_entry(const XpBookServices* services, int32_t index, uint8_t toc);
typedef struct XpBookItemSizeServices {
  void* ctx;
  size_t (*operation)(void*, uint32_t operation, size_t index);
} XpBookItemSizeServices;
size_t xp_book_item_size(const XpBookItemSizeServices* services, uint16_t index, uint8_t zip);

uint8_t xp_book_metadata_cleanup(const XpBookServices* services);

#ifdef __cplusplus
}
#endif
