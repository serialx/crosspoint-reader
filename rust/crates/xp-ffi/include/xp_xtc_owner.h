#pragma once

#include "xp_ffi.h"

#ifdef __cplusplus
extern "C" {
#endif

// Caller storage is initialized once at its final address, serialized, then dropped once.
// It cannot move while initialized. Native file context and leaves outlive this storage.
enum { XP_XTC_OWNER_WORDS = sizeof(uintptr_t) == 4 ? 40 : 26 };
typedef struct {
  uintptr_t words[XP_XTC_OWNER_WORDS];
} XpXtcOwnerStorage;

// Borrowed rows, including names, remain valid until close, reopen, or owner drop.
// inline_name is owner-private storage preserving the native short-name allocation shape.
// Copying a row never transfers ownership of its name.
typedef struct {
  const uint8_t* name;
  size_t name_len;
  uint8_t inline_name[16];
  uint16_t start_page, end_page;
} XpXtcChapterRow;

typedef struct {
  const XpBookXtcState* state;
  const uint8_t* title;
  size_t title_len;
  const uint8_t* author;
  size_t author_len;
  const XpXtcChapterRow* chapters;
  size_t chapter_count;
} XpXtcView;

enum { XP_XTC_ALLOC_TEXT, XP_XTC_ALLOC_SHRINK, XP_XTC_ALLOC_CHAPTERS, XP_XTC_ALLOC_STREAM };

// File callbacks use the existing caller-provided HalFile, retaining its Impl across close.
// allocate takes bytes except CHAPTERS takes row count, preserving native max_size checks.
// TEXT/CHAPTERS/STREAM preserve native throwing failure; SHRINK is optional storage
// and returns null when native shrink would not allocate or would fail.
// release matches allocate. Diagnostics may borrow view(), never mutate or reenter IO.
typedef struct {
  bool (*open)(void*, const uint8_t*);
  bool (*is_open)(void*);
  void (*close)(void*);
  int32_t (*read)(void*, uint8_t*, size_t);
  bool (*seek)(void*, uint64_t);
  uint64_t (*size)(void*);
  uint8_t* (*allocate)(size_t, uint32_t kind);
  void (*release)(uint8_t*, size_t);
  void (*diagnostic)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
} XpXtcLeaves;

typedef struct {
  void* context;
  void (*emit)(void*, const uint8_t*, size_t, size_t);
} XpXtcStream;

void xp_xtc_owner_init(XpXtcOwnerStorage*, void* file_context, const XpXtcLeaves*);
void xp_xtc_owner_drop(XpXtcOwnerStorage*);
int32_t xp_xtc_owner_open(XpXtcOwnerStorage*, const uint8_t* path, size_t path_len);
void xp_xtc_owner_close(XpXtcOwnerStorage*);
// One snapshot publishes all metadata; lazy=true performs the existing lazy chapter read first.
void xp_xtc_owner_view(XpXtcOwnerStorage*, bool lazy, XpXtcView*);
bool xp_xtc_owner_page_info(XpXtcOwnerStorage*, uint32_t index, XpXtcEntry*);
size_t xp_xtc_owner_load(XpXtcOwnerStorage*, uint32_t index, uint8_t*, size_t);
int32_t xp_xtc_owner_stream(XpXtcOwnerStorage*, uint32_t index, size_t chunk_size, const XpXtcStream*);

#ifdef __cplusplus
}
#endif
