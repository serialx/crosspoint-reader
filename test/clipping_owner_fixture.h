#pragma once
#include <xp_epub_session.h>

struct XpEpubTestClippingSeed {
  void* native;
  void* section;
  int32_t spine;
  int32_t page;
  uint16_t width;
  uint16_t height;
  uint8_t historical;
  void* context;
  uint32_t (*signature)(void*, uint16_t, uint16_t);
  void (*nonempty)(void*, uint8_t);
};
extern "C" {
void* xp_epub_test_clipping_create(const XpEpubTestClippingSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                   const XpAppResources*);
void xp_epub_test_clipping_destroy(void*);
int32_t xp_epub_test_clipping_hit(void*, void* page, int32_t x, int32_t y);
void xp_epub_test_clipping_draw(void*, void* page, int32_t font, int32_t top, int32_t left);
uint32_t xp_epub_test_clipping_signature(void*);
uint8_t xp_epub_test_clipping_nonempty(void*);
}
