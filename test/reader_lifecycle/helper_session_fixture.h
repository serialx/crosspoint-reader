#pragma once

#include <xp_epub_session.h>

#include "legacy_page_fixture.h"

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
void* xp_epub_test_helpers_create_legacy_render(const XpEpubTestHelpersSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                                const XpAppResources*, const XpEpubLegacyPageServices* = nullptr);
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
