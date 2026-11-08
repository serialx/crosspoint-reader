#pragma once
#include "xp_font_registry.h"
#ifdef __cplusplus
extern "C" {
#endif
// Reusable by UI-SCENE: no Page, text or command pointer is retained here.
// Geometry and pixel storage are an exclusive synchronous renderer loan.
typedef struct {
  XpGlyphTarget glyphs;
  const XpLoadedFonts* fonts;
  void* native;
  uint8_t inverted;
} XpPageTarget;
typedef struct {
  int32_t font, x, y;
  uint8_t mode;
  XpPagePlan* plan;
  XpPageRowBounds* bounds;
  size_t count;
  uint8_t prepare;
} XpPagePaint;
// Native-origin Definition/selection adapter. Rust page owners call the Rust
// function directly. Arena/plan/source loans follow the existing page contract.
uint32_t xp_page_paint(const uint8_t* arena, size_t, const XpPageTarget*, const XpPagePaint*);
void xp_native_render_target(XpPageTarget* out);
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
  XP_IMAGE_FILE_LARGEST_BLOCK
} XpPageImageFileOp;
// Physical SDK file/memory operations; paths are terminated, bytes borrowed.
size_t xp_native_page_image_file(XpPageImageFile*, XpPageImageFileOp, const uint8_t* path, uint8_t*, size_t);
// One decode/raster leaf. 0 no decoder, 1 decode failed, 2 success.
uint8_t xp_native_page_image_decode(void* native, const XpPageImage*, const uint8_t* cache);
// Inherited book extraction facade, retired by B3 when Rust owns extraction.
uint8_t xp_native_page_image_extract(const XpPageImage*, uint8_t query_only);
void xp_native_page_image_log(void* native, const XpPageImage*, const uint8_t* cache, uint32_t event, int32_t a,
                              int32_t b);
#ifdef __cplusplus
}
#endif
