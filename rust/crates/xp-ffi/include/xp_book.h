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

#ifdef __cplusplus
}
#endif
