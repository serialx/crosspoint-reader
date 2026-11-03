#pragma once
#include <xp_epub_session.h>

#include "legacy_book_fixture.h"

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
void* xp_epub_test_position_create_legacy_book(const XpEpubTestPositionSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                               const XpAppResources*, const XpEpubLegacyBookServices*);
void xp_epub_test_position_destroy(void*);
void xp_epub_test_position_loop(void*, XpEpubPosition*);
void xp_epub_test_position_borrowed(void*, XpEpubPosition*);
}
