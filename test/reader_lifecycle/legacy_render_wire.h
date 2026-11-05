#pragma once
#include <stdint.h>

// Frozen wire inputs for named synthetic reader oracles only.
/* Callback group: page-render sequencing, outside ModelLock. Rust issues one
 * synchronous operation at a time; renderer/storage/allocator leaves keep their
 * existing allocation and locking behavior. Null/missing services do nothing.
 * No new page renderer, task, scheduler, retained callback or Rust heap owner. */
typedef enum {
  XP_UI_READER_RENDER_NOW,
  XP_UI_READER_RENDER_HAS_IMAGES,
  XP_UI_READER_RENDER_IMAGES_NEED_DECODE,
  XP_UI_READER_RENDER_TAKE_FORCED,
  XP_UI_READER_RENDER_COUNTER,
  XP_UI_READER_RENDER_FREQUENCY,
  XP_UI_READER_RENDER_TEXT_AA,
  XP_UI_READER_RENDER_IS_A4,
  XP_UI_READER_RENDER_IS_BW = 19,
  XP_UI_READER_RENDER_IS_X3 = 8,
  XP_UI_READER_RENDER_IS_UC8279,
  XP_UI_READER_RENDER_ABSOLUTE_SUPPORTED,
  XP_UI_READER_RENDER_OVERLAY_CAPS,
  XP_UI_READER_RENDER_ABSOLUTE_CAPS,
  XP_UI_READER_RENDER_STRIP_BOARD,
  XP_UI_READER_RENDER_ELEMENTS,
  XP_UI_READER_RENDER_WIDTH_BYTES,
  XP_UI_READER_RENDER_HEIGHT,
  XP_UI_READER_RENDER_FREE_HEAP,
  XP_UI_READER_RENDER_MAX_BLOCK
} XpUiReaderRenderQuery;
/* Capability bits returned by *_CAPS: STRIP=1, COMBINED=2, ASYNC_BASE=4. */
typedef enum {
  XP_UI_READER_RENDER_IMAGE_RESET,
  XP_UI_READER_RENDER_PREWARM_BEGIN,
  XP_UI_READER_RENDER_PAGE,
  XP_UI_READER_RENDER_STATUS,
  XP_UI_READER_RENDER_PREWARM_END,
  XP_UI_READER_RENDER_HIGHLIGHTS,
  XP_UI_READER_RENDER_IMAGES,
  XP_UI_READER_RENDER_PLACEHOLDERS,
  XP_UI_READER_RENDER_CLEAR,
  XP_UI_READER_RENDER_DISPLAY,
  XP_UI_READER_RENDER_DISPLAY_ASYNC,
  XP_UI_READER_RENDER_GRAY_BASE,
  XP_UI_READER_RENDER_ABSOLUTE_BASE,
  XP_UI_READER_RENDER_PRECONDITION,
  XP_UI_READER_RENDER_SET_COUNTER,
  XP_UI_READER_RENDER_PREPARE_STRIPS,
  XP_UI_READER_RENDER_IMAGE_WINDOW,
  XP_UI_READER_RENDER_RECORD_BEGIN,
  XP_UI_READER_RENDER_RECORD_END,
  XP_UI_READER_RENDER_SET_SOURCE,
  XP_UI_READER_RENDER_CLEAR_PLAN,
  XP_UI_READER_RENDER_CLEAR_WINDOW,
  XP_UI_READER_RENDER_RELEASE_STRIPS,
  XP_UI_READER_RENDER_ALLOCATE,
  XP_UI_READER_RENDER_BORROW_SCRATCH,
  XP_UI_READER_RENDER_MODE,
  XP_UI_READER_RENDER_STRIP_BEGIN,
  XP_UI_READER_RENDER_STRIP_END,
  XP_UI_READER_RENDER_WAIT,
  XP_UI_READER_RENDER_WRITE_PLANE,
  XP_UI_READER_RENDER_GRAY_DISPLAY,
  XP_UI_READER_RENDER_CLEANUP,
  XP_UI_READER_RENDER_STORE_BW,
  XP_UI_READER_RENDER_RESTORE_BW,
  XP_UI_READER_RENDER_COPY_LSB,
  XP_UI_READER_RENDER_COPY_MSB,
  XP_UI_READER_RENDER_LOG,
  XP_UI_READER_RENDER_MARK
} XpUiReaderRenderOperation;
typedef enum { XP_UI_READER_BUFFER_LSB, XP_UI_READER_BUFFER_MSB, XP_UI_READER_BUFFER_SCRATCH } XpUiReaderRenderBuffer;
typedef enum {
  XP_UI_READER_RENDER_STRIP_OOM,
  XP_UI_READER_RENDER_ABSOLUTE_FAILED,
  XP_UI_READER_RENDER_ABSOLUTE_QUALITY,
  XP_UI_READER_RENDER_SCRATCH_OOM,
  XP_UI_READER_RENDER_STORE_FAILED,
  XP_UI_READER_RENDER_TIMING_ASYNC,
  XP_UI_READER_RENDER_TIMING_TILED,
  XP_UI_READER_RENDER_TIMING_GRAY,
  XP_UI_READER_RENDER_TIMING_BW
} XpUiReaderRenderLog;
typedef struct {
  uint8_t kind, buffer, mode, reserved;
  uint16_t y, rows;
  uint32_t offset, value;
} XpUiReaderRenderOp;
/* mode uses XpUiRefresh for display operations, 0 BW/1 LSB/2 MSB for MODE.
 * value carries allocation bytes, counter, log/mark ID or clear fill.
 * MARK stores a timestamp in the C++ diagnostic context; LOG only formats the
 * original statement. Rust decides all conditions and operation ordering.
 * Results: allocation/store/base/prepare/source operations return success;
 * RECORD_END returns page-plan validity; other operations return zero. */
typedef struct {
  void* context;
  uint32_t (*read)(void*, uint8_t query);
  uint32_t (*execute)(void*, const XpUiReaderRenderOp*);
} XpUiReaderRenderServices;

