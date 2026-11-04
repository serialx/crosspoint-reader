#pragma once

#include <xp_epub_session.h>

#include "legacy_page_fixture.h"

struct XpEpubTestLinkSeed {
  void* native;
};

extern "C" {
void* xp_epub_test_link_create(const XpEpubTestLinkSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                               const XpAppResources*, const XpEpubLegacyPageServices*);
void xp_epub_test_link_destroy(void*);
uint8_t xp_epub_test_link_at_point(void*, int32_t x, int32_t y, int32_t left, int32_t top, uint16_t* index);
size_t xp_epub_test_sanitize_summary(XpAppText input, uint8_t (*output)[72]);
}
