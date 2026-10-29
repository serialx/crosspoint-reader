#pragma once
#include <stddef.h>
#include <stdint.h>

#include "xp_ffi.h"
#ifdef __cplusplus
extern "C" {
#endif
// Synchronous borrowed spans, readable for len bytes (null only for zero length).
// Callbacks retain no pointers and must not reenter their current operation.
typedef struct {
  const uint8_t* data;
  size_t len;
} XpBookBorrow;
uint8_t xp_book_bidi_should_shape(const uint8_t*);
uint8_t xp_book_bidi_word_count(size_t);
uint8_t xp_book_bidi_word_spans(const uint8_t*, size_t, const uint16_t*, size_t, size_t);
uint8_t xp_book_xtc_progress(uint8_t, uint32_t, uint32_t);
XpBookBorrow xp_book_xtc_title(XpBookBorrow, XpBookBorrow);
uint32_t xp_book_txt_image_kind(XpBookBorrow);
const char* xp_book_txt_cache_tag(XpBookBorrow);
uint8_t xp_book_discover_css(XpBookBorrow, XpBookBorrow, size_t, void*, XpBookBorrow (*)(void*, size_t));
typedef struct XpBookDocumentServices {
  void* ctx;
  size_t (*call)(void*, uint32_t, size_t);
  XpBookBorrow (*value)(void*, uint32_t);
  void (*assign)(void*, uint32_t, XpBookBorrow);
  void (*diagnostic)(void*, uint32_t);
} XpBookDocumentServices;
uint8_t xp_book_document(const XpBookDocumentServices*, uint32_t, uint8_t);
typedef struct {
  void* ctx;
  size_t (*call)(void*, uint32_t, size_t, size_t);
  void (*diagnostic)(void*, uint32_t, size_t, size_t);
} XpBookCssParseServices;
uint32_t xp_book_css_parse_files(const XpBookCssParseServices*, size_t, uint8_t);
typedef struct XpBookZipIndexServices {
  void* ctx;
  size_t (*control)(void*, uint32_t, uint32_t);
  bool (*next)(void*, XpZipEntry*);
  XpBookBorrow (*name)(void*, const XpZipEntry*, bool);
  bool (*cache)(void*, uint32_t, const XpZipEntry*, XpBookBorrow);
} XpBookZipIndexServices;
uint8_t xp_book_zip_index(const XpBookZipIndexServices*, uint32_t, XpBookBorrow, XpBookBorrow);
bool xp_book_zip_populate(const XpBookZipIndexServices*);
typedef struct {
  uint64_t hash;
  uint16_t len, index;
} XpBookZipTarget;
size_t xp_book_zip_batch(const XpBookZipIndexServices*, size_t, void*, XpBookZipTarget (*)(void*, size_t),
                         bool (*)(void*, uint16_t, uint32_t));
typedef struct {
  void* ctx;
  bool (*open)(void*);
  size_t (*size)(void*);
  uint8_t* (*allocate)(void*, size_t);
  bool (*read)(void*, uint32_t, uint8_t*, size_t);
  void (*save)(void*, const XpZipDetails*);
  void (*diagnostic)(void*, uint32_t);
} XpBookZipDetailServices;
bool xp_book_zip_details(const XpBookZipDetailServices*, bool);
void xp_book_zip_close(uint32_t*, bool*, bool*);
int32_t xp_book_find_spine(XpBookBorrow, size_t, bool, void*, XpBookBorrow (*)(void*, size_t));
typedef struct {
  uint16_t width, height;
  uint8_t depth;
} XpBookXtcInfo;
typedef struct {
  void* ctx;
  size_t (*call)(void*, uint32_t);
  XpBookXtcInfo (*info)(void*);
  bool (*prepare)(void*, size_t, size_t, const XpBookXtcThumbnail*);
  void (*diagnostic)(void*, uint32_t, size_t);
} XpBookXtcCoverServices;
bool xp_book_xtc_generate(const XpBookXtcCoverServices*, bool, int32_t);
void xp_book_directories(XpBookBorrow, void*, bool (*)(void*, uint32_t, XpBookBorrow));
XpBookBorrow xp_book_txt_title(XpBookBorrow);
#ifdef __cplusplus
enum class XpBookPath : uint32_t {
  HtmlDirectory,
  SectionsDirectory,
  HtmlDocument,
  TextHref,
  Cover,
  ThumbPattern,
  ThumbPrefix
};
#else
typedef uint32_t XpBookPath;
#endif
const char* xp_book_path(XpBookPath);
void xp_book_txt_metadata(void*, XpBookBorrow (*)(void*, uint32_t, XpBookBorrow));
typedef struct {
  int32_t index;
  uint32_t diagnostic;
} XpBookLookup;
XpBookLookup xp_book_lookup(bool, int32_t, int32_t, bool);
void xp_book_reclaim_content(size_t, void (*)(), void (*)(size_t));
XpBookLookup xp_book_toc_spine(int32_t);
XpBookBorrow xp_book_href_path(XpBookBorrow);
#ifdef __cplusplus
}
#endif
