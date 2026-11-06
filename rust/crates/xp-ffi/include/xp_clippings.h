#pragma once

#include "xp_app.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint64_t revision;
  uint32_t timestamp;
  uint32_t signature;
  uint32_t start_offset;
  uint32_t end_offset;
  uint32_t text_offset;
  uint16_t spine;
  uint16_t start_page;
  uint16_t end_page;
  uint16_t page_count;
  uint16_t start_word;
  uint16_t end_word;
  uint16_t word_count;
  uint16_t paragraph;
  uint16_t text_length;
  uint8_t pending;
  uint8_t id[65];
  uint8_t chapter[48];
} XpClippingRecord;

typedef enum {
  XP_CLIPPING_OPEN_READ = 0,
  XP_CLIPPING_OPEN_REWRITE = 1,
  XP_CLIPPING_OPEN_APPEND = 2,
  XP_CLIPPING_OPEN_READ_WRITE = 3,
} XpClippingOpenMode;

// Physical HalFile operations absent from the existing app/EPUB tables.
typedef struct XpClippingPhysical {
  void* (*open)(XpAppText path, XpClippingOpenMode mode);
  uint8_t (*seek_relative)(void* file, uint32_t offset);
  int32_t (*available)(void* file);
  uint8_t (*truncate)(void* file, uint32_t size);
  uint8_t (*is_directory)(void* file);
  void* (*next)(void* directory, uint8_t* failed);
  uint32_t (*name)(void* file, uint8_t* output, uint32_t capacity);
  int64_t (*epoch_seconds)(void);
  void (*random)(uint8_t* output, uint32_t length);
} XpClippingPhysical;

// P18 native sync compatibility. Spans are borrowed only during each call;
// records are copied out and never point into the store's index.
uint8_t xp_clippings_p18_load(XpAppText path, XpAppText title, XpAppText author, XpAppText kind);
void xp_clippings_p18_unload(void);
uint32_t xp_clippings_p18_count(void);
uint8_t xp_clippings_p18_copy_row(uint32_t index, XpClippingRecord* output);
uint8_t xp_clippings_p18_read_text(uint32_t index, uint8_t* output, uint32_t capacity, uint32_t* length);
uint8_t xp_clippings_p18_prepare_sync(void);
uint8_t xp_clippings_p18_finish_uploads(void);
uint8_t xp_clippings_p18_apply_remote(const XpClippingRecord* record, XpAppText text, uint8_t deleted);
uint8_t xp_clippings_p18_next_deletion(uint32_t* offset, uint8_t output[65]);
uint8_t xp_clippings_p18_finish_deletions(void);

// FileBrowser/BookCacheUtils (P17), WebDAV/web/sync (P18).
uint8_t xp_clippings_p17_p18_move_book(XpAppText from, XpAppText to);
uint8_t xp_clippings_p17_p18_delete_for_path(XpAppText path, XpAppText kind);

#ifdef __cplusplus
}
static_assert(sizeof(XpClippingRecord) == 160, "XpClippingRecord layout");
#else
_Static_assert(sizeof(XpClippingRecord) == 160, "XpClippingRecord layout");
#endif
