#pragma once

#include <xp_epub_session.h>

struct XpEpubTestPositionSeed {
  void* native;
  void* book;
  void* section;
  int32_t page;
  int32_t spine;
  void* estimate_context;
  void (*estimate_read)(void*, int32_t);
};

extern "C" {
void* xp_epub_test_position_create(const XpEpubTestPositionSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                   const XpAppResources*);
void xp_epub_test_position_destroy(void*);
void xp_epub_test_position_loop(void*, XpEpubPosition*);
void xp_epub_test_position_borrowed(void*, XpEpubPosition*);
}
