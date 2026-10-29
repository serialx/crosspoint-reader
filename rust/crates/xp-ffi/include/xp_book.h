#pragma once
#include <stddef.h>
#include <stdint.h>

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

#ifdef __cplusplus
}
#endif
