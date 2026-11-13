#pragma once
#include "xp_font_registry.h"
#include "xp_ui.h"
#ifdef __cplusplus
extern "C" {
#endif
// Exclusive synchronous geometry/pixel loan, reusable by UI-SCENE; no Page/text/command pointer is retained.
typedef struct {
  XpGlyphTarget glyphs;
  const XpLoadedFonts* fonts;
  void* native;
  uint8_t inverted;
} XpPageTarget;
/* Native-origin xp_page_paint request for rich dictionary, selection, settings preview and Details HTML.
 * Rust owners call Rust directly; loans follow the page contract.
 * mode: 0 all elements, 1 images only, 2 all elements with image placeholders. */
typedef struct {
  int32_t font, x, y;
  uint8_t mode;
  XpPagePlan* plan;
  XpPageRowBounds* bounds;
  size_t count;
  uint8_t prepare;
} XpPagePaint;
uint32_t xp_page_paint(const uint8_t* arena, size_t, const XpPageTarget*, const XpPagePaint*);
// Lends UI clear/text services when requested; returns whether a font cache is bound.
// Scan state is read directly from the Rust owner after this physical loan returns.
uint8_t xp_native_render_target(XpPageTarget* out, XpUiPaint* scene_paint);
typedef struct {
  const uint8_t* path;
  const uint8_t* source;
  size_t path_len, source_len;
  int32_t x, y, width, height;
} XpPageImage;
// HalFile stays inline. Host fixtures use a larger pointer-width-specific cell.
typedef struct {
  uintptr_t words[sizeof(uintptr_t) == 4 ? 3 : 8];
} XpPageImageFile;
typedef enum {
  XP_IMAGE_FILE_INIT,
  XP_IMAGE_FILE_DROP,
  XP_IMAGE_FILE_OPEN,
  XP_IMAGE_FILE_READ,
  XP_IMAGE_FILE_SEEK,
  XP_IMAGE_FILE_SIZE,
  XP_IMAGE_FILE_CLOSE,
  XP_IMAGE_FILE_EXISTS,
  XP_IMAGE_FILE_FREE_HEAP,
  XP_IMAGE_FILE_LARGEST_BLOCK,
  XP_IMAGE_FILE_OPEN_WRITE,
  XP_IMAGE_FILE_FLUSH,
  XP_IMAGE_FILE_REMOVE,
  XP_IMAGE_FILE_POSITION,
  XP_IMAGE_FILE_READ_RAW,
  XP_IMAGE_FILE_OPEN_PNG,
  XP_IMAGE_FILE_OPEN_GIF,
  XP_IMAGE_FILE_OPEN_CACHE_WRITE,
  XP_IMAGE_FILE_IS_OPEN,
  XP_IMAGE_FILE_NOW,
  XP_IMAGE_FILE_YIELD,
  XP_IMAGE_FILE_FREE_INTERNAL_HEAP
} XpPageImageFileOp;
// Physical SDK file, memory, clock and yield operations; paths are terminated, bytes borrowed.
size_t xp_native_page_image_file(XpPageImageFile*, XpPageImageFileOp, const uint8_t* path, uint8_t*, size_t);
// Writes borrowed bytes to the caller-owned HalFile; never calls Rust, including for protected streams.
uint8_t xp_native_page_image_write(void* file, const uint8_t* bytes, size_t length);
typedef enum {
  XP_IMAGE_CODEC_BEGIN = 50,
  XP_IMAGE_CODEC_OPEN,
  XP_IMAGE_CODEC_SEEK,
  XP_IMAGE_CODEC_READ,
  XP_IMAGE_CODEC_PROBE_OPEN,
  XP_IMAGE_CODEC_PROBE_READ,
  XP_IMAGE_CODEC_PROBE_VALIDATION
} XpPageImageCodecEvent;
typedef struct {
  const XpPageImage* image;
  const uint8_t* cache;
  XpPageImageFile* input;
  uint32_t free_heap, elapsed;
  int32_t color, depth;
  uint8_t format, seek_failed;
} XpPageImageCodecLog;
// Borrows diagnostic state and paths; formatting does not retain or call Rust.
void xp_native_page_image_codec_log(const XpPageImageCodecLog*, uint32_t event, uint32_t a, uint32_t b, uint32_t c,
                                    uint32_t d);
// JPEGDEC facts and synchronous row loan. Rust owns close/pipeline-drop/free order.
// Set rows before OPEN; DECODE argument is the scale denominator (2/4/8, otherwise 1).
typedef struct {
  void* decoder;
  const uint8_t* path;
  void* rows;
  int32_t width, height, result, error;
  uint8_t progressive;
} XpPageJpeg;
typedef enum {
  XP_PAGE_JPEG_ALLOCATE,
  XP_PAGE_JPEG_OPEN,
  XP_PAGE_JPEG_FACTS,
  XP_PAGE_JPEG_CONFIGURE,
  XP_PAGE_JPEG_DECODE,
  XP_PAGE_JPEG_CLOSE,
  XP_PAGE_JPEG_FREE
} XpPageJpegOp;
uint32_t xp_native_page_jpeg(XpPageJpeg*, XpPageJpegOp, uint32_t argument);
// XpBookJpegEvent: heap events carry free bytes, TIME elapsed ms; other events ignore argument.
// Event 12 shares ImageDiagnostic::DecodeFailed's value; argument is xp_book_image_dimensions status.
enum { XP_PAGE_JPEG_BAD_DIMENSIONS = 12 };
void xp_native_page_jpeg_log(const XpPageJpeg*, uint32_t event, uint32_t argument);
#ifdef XP_APP_TEST
// Historical native extraction path for host oracles.
uint8_t xp_native_page_image_extract(const XpPageImage*, uint8_t query_only);
#endif
// Formats an image event through the native logger without retaining borrowed paths.
void xp_native_page_image_log(void* native, const XpPageImage*, const uint8_t* cache, uint32_t event, int32_t a,
                              int32_t b);
#ifdef __cplusplus
}
#endif
