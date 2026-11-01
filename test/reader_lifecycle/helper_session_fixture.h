#pragma once

#include <xp_epub_session.h>

struct XpEpubTestHelpersSeed {
  void* native;
  void* popup;
  int32_t refresh;
  uint8_t stored;
  uint8_t pending;
};

struct XpEpubTestHelpersObservation {
  int32_t refresh;
  uint8_t stored;
  uint8_t pending;
};

extern "C" {
void* xp_epub_test_helpers_create(const XpEpubTestHelpersSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                  const XpAppResources*);
void xp_epub_test_helpers_destroy(void*);
void xp_epub_test_helpers_observe(void*, XpEpubTestHelpersObservation*);
void xp_epub_test_helpers_discard(void*);
void xp_epub_test_helpers_apply_text_live(void*);
void xp_epub_test_helpers_push(void*);
void xp_epub_test_helpers_settle(void*);
void xp_epub_test_helpers_paint(void*);
void xp_epub_test_helpers_disable_fast(void*);
void xp_epub_test_helpers_orientation(void*);
}
