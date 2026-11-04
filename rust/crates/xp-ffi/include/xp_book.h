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
  XP_BOOK_FILE_RELEASE_TEXT = 5,
} XpBookFileOperation;

typedef enum XpBookSectionDiagnostic {
  XP_BOOK_SECTION_SHORT_RECORD = 0,
  XP_BOOK_SECTION_INVALID_RECORD = 1,
  XP_BOOK_SECTION_INVALID_STRING = 2,
  XP_BOOK_SECTION_VISIBLE_SEEK = 3,
  XP_BOOK_SECTION_VISIBLE_READ = 4,
} XpBookSectionDiagnostic;

typedef struct XpBookFileServices {
  void* ctx;
  size_t (*operation)(void* ctx, uint32_t operation, size_t argument);
  size_t (*read)(void* ctx, uint8_t* bytes, size_t length);
  uint8_t* (*resize_text)(void* ctx, size_t length);
  void (*diagnostic)(void* ctx, uint32_t event, uint32_t a, uint32_t b);
} XpBookFileServices;

typedef enum XpBookSectionQuery {
  XP_BOOK_SECTION_PAGE_COUNT = 1,
  XP_BOOK_SECTION_PARAGRAPH_PAGE = 3,
  XP_BOOK_SECTION_LIST_PAGE = 4,
} XpBookSectionQuery;

// output has three slots; these cache queries use slot 0.
uint8_t xp_book_section_query(const XpBookFileServices* services, uint32_t query, uint32_t index, uint32_t output[3]);
uint8_t xp_book_section_anchor(const XpBookFileServices* services, const uint8_t* needle, size_t length,
                               uint32_t* output);

const char* xp_book_entity(const uint8_t* input, size_t length);
uint32_t xp_book_layout_signature(const uint32_t fields[12], float compression);
uint8_t xp_book_link_contains(const int16_t bounds[4], int32_t x, int32_t y, int32_t slop, int32_t minimum);
uint8_t xp_book_tag_hidden(const uint8_t* name, size_t length);
// 0 accepted, 1 nonpositive, 2 dimension limit, 3 pixel limit.
uint8_t xp_book_image_dimensions(int64_t width, int64_t height);
uint8_t xp_book_image_yield(uint32_t* last, uint32_t now);

typedef enum XpBookCacheOperation {
  XP_BOOK_CACHE_RESET_WRITER = 0,
  XP_BOOK_CACHE_OPEN = 1,
  XP_BOOK_CACHE_CLOSE = 3,
  XP_BOOK_CACHE_RESET_WINDOW = 8,
  XP_BOOK_CACHE_RESERVE_WINDOW = 9,
  XP_BOOK_CACHE_SEEK = 10,
  XP_BOOK_CACHE_ADD_WINDOW_START = 12,
  XP_BOOK_CACHE_WINDOW_START = 20,
  XP_BOOK_CACHE_WINDOW_COUNT = 22,
  XP_BOOK_CACHE_LOCK = 23,
  XP_BOOK_CACHE_FILL_FILE = 24,
  XP_BOOK_CACHE_FILL_SEEK = 25,
  XP_BOOK_CACHE_FILL_POSITION = 29,
  XP_BOOK_CACHE_PREPARE_TEMPORARY = 35,
  XP_BOOK_CACHE_TEMPORARY_EXISTS = 36,
  XP_BOOK_CACHE_TEMPORARY_REMOVE = 37,
  XP_BOOK_CACHE_DROP_TEMPORARY = 38,
  XP_BOOK_CACHE_SPINE_RECORD = 39,
} XpBookCacheOperation;
typedef enum XpBookCacheField {
  XP_BOOK_FIELD_LUT,
  XP_BOOK_FIELD_SPINE_COUNT,
  XP_BOOK_FIELD_TOC_COUNT,
  XP_BOOK_FIELD_TITLE,
  XP_BOOK_FIELD_AUTHOR,
  XP_BOOK_FIELD_LANGUAGE,
  XP_BOOK_FIELD_COVER,
  XP_BOOK_FIELD_TEXT,
  XP_BOOK_FIELD_HREF,
  XP_BOOK_FIELD_CUMULATIVE,
  XP_BOOK_FIELD_TOC_INDEX,
  XP_BOOK_FIELD_TOC_TITLE,
  XP_BOOK_FIELD_TOC_HREF,
  XP_BOOK_FIELD_TOC_ANCHOR,
  XP_BOOK_FIELD_LEVEL,
  XP_BOOK_FIELD_SPINE_INDEX,
  XP_BOOK_FIELD_BOOK_SIZE,
  XP_BOOK_FIELD_LOADED,
  XP_BOOK_FIELD_WINDOW_FIRST,
  XP_BOOK_FIELD_WINDOW,
} XpBookCacheField;
// Native storage backs field results: initialized little-endian POD or a string
// resized to size bytes. WINDOW + index (0..XP_BOOK_METADATA_WINDOW-1) lends a u32.
// Rust borrows these bytes within the service call; it must not retain or free
// them. Strings remain valid until resized or their native record is released.
typedef struct XpBookCacheServices {
  void* ctx;
  uint32_t (*operation)(void*, uint32_t, uint32_t);
  size_t (*read)(void*, uint32_t file, uint8_t*, size_t);
  uint8_t* (*field)(void*, uint32_t field, size_t size);
  void (*diagnostic)(void*, uint32_t event, uint32_t argument);
} XpBookCacheServices;
uint8_t xp_book_metadata_load(const XpBookCacheServices* services);
uint32_t xp_book_metadata_cumulative(const XpBookCacheServices* services, int32_t index);
int32_t xp_book_metadata_spine(const XpBookCacheServices* services, uint32_t size);

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
  XP_BOOK_CSS_IO_AVAILABLE = 15,
  XP_BOOK_CSS_IO_SKIP = 16,
  XP_BOOK_CSS_IO_ALLOCATE_BUFFER = 19,
  XP_BOOK_CSS_IO_READ_BUFFER = 20,
  XP_BOOK_CSS_IO_ALLOCATE_ORDER = 27,
  XP_BOOK_CSS_IO_WRITE_BUFFER = 31,
  XP_BOOK_CSS_IO_WRITE_WORD = 32,
  XP_BOOK_CSS_IO_DROP_ORDER = 33,
  XP_BOOK_CSS_IO_SOURCE_VALID = 34,
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
  XP_BOOK_CSS_IO_EVENT_HANDLE_OOM = 18,
} XpBookCssEvent;
typedef struct XpCss XpCss;
typedef struct XpCssDiagnostics XpCssDiagnostics;
// Borrowed slot and native-owned buffers; callbacks only replace buffer/order pointers.
typedef struct {
  XpCss** owner;
  uint8_t* buffer;
  uint16_t* order;
  uint8_t word[4];
  size_t capacity;
} XpBookCssIo;
uint32_t xp_book_css(const XpBookServices*, XpBookCssIo*, const XpCssDiagnostics*, uint32_t action, uint8_t complete);

uint8_t xp_book_section_visible(const XpBookFileServices* io, uint32_t offset, uint8_t first, uint32_t* page);

typedef struct XpBookTextSpan {
  const uint8_t* data;
  size_t length;
} XpBookTextSpan;

typedef struct XpChapter XpChapter;
typedef enum XpBookParseOperation {
  XP_BOOK_PARSE_ENSURE = 0,
  XP_BOOK_PARSE_RESET_STOP = 1,
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
  XP_BOOK_PARSE_RELEASE_BUFFER = 16,
  XP_BOOK_PARSE_IS_OPEN = 17,
  XP_BOOK_PARSE_CLOSE = 18,
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
/* Handle/buffer/result are borrowed only for this call. The buffer view expires on
 * buffer mutation; feed copies it before callbacks. result packs status/error/line
 * in bits 0..7/8..31/32..63 and is set before the diagnostic callback. */
uint32_t xp_book_parse(const XpBookServices* services, XpChapter** chapter, const uint8_t* (*buffer)(void*),
                       uint64_t* result, uint32_t action);

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
/* resolved is updated by RESOLVE and stays valid until ACCEPT; counter is exclusive.
 * PREPARE_CACHE receives the suffix offset, including length for an empty suffix. */
uint8_t xp_book_probe_image(const XpBookServices* services, const XpBookTextSpan* resolved, int32_t* counter);

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

// sizes value is callback-local: PATH/HREF/NORMALIZE write XpPkgSpan; LOOKUP
// reads XpPkgSpan. APPEND/SET_TARGET read three u64s (hash, length, spine index);
// TARGET writes that triple. Other operations use null. Caller owns returned
// span storage: PATH lasts through the build; HREF until resized or END_ENTRY;
// NORMALIZE until replaced or END_ENTRY. Rust must not free or retain it longer.
// index is the size count, href byte length, target/cached index, or diagnostic
// argument. RAW_SIZE/CACHED return bytes, OPEN_ZIP success, FILL matched count;
// LOOKUP: size in bits 0..31, failure in bits 32..63. Other allocating ops: 0 or bit 63 (OOM).
// BEGIN/END_ENTRY own entry strings; ALLOCATE/CLEAR/DROP_TARGETS own targets;
// ALLOCATE_SIZES owns sizes until XP_PKG_END_SIZES also releases the ZIP.
typedef enum XpBookSizeOperation {
  XP_BOOK_SIZE_PATH = 0,
  XP_BOOK_SIZE_RAW_SIZE = 1,
  XP_BOOK_SIZE_OPEN_ZIP = 2,
  XP_BOOK_SIZE_ALLOCATE_TARGETS = 3,
  XP_BOOK_SIZE_ALLOCATE_SIZES = 4,
  XP_BOOK_SIZE_BEGIN_ENTRY = 5,
  XP_BOOK_SIZE_HREF = 6,
  XP_BOOK_SIZE_NORMALIZE = 7,
  XP_BOOK_SIZE_APPEND = 8,
  XP_BOOK_SIZE_END_ENTRY = 9,
  XP_BOOK_SIZE_TARGET = 10,
  XP_BOOK_SIZE_SET_TARGET = 11,
  XP_BOOK_SIZE_FILL = 12,
  XP_BOOK_SIZE_CLEAR = 13,
  XP_BOOK_SIZE_DROP_TARGETS = 14,
  XP_BOOK_SIZE_CACHED = 15,
  XP_BOOK_SIZE_LOOKUP = 16,
  XP_BOOK_SIZE_ERROR = 17,
  XP_BOOK_SIZE_BATCH = 18,
  XP_BOOK_SIZE_MATCHED = 19,
} XpBookSizeOperation;

uint8_t xp_book_metadata_entry(const XpBookCacheServices* services, int32_t index, uint8_t toc);
uint8_t xp_book_metadata_cleanup(const XpBookCacheServices* services);

#ifdef __cplusplus
}
#endif
