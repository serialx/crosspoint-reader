#pragma once
#include <xp_epub_session.h>
struct XpEpubTestRenderSeed {
  void* native;
  void* context;
  int32_t counter;
  uint8_t forced;
  void (*status)(void*);
  void (*highlights)(void*, void*, int32_t, int32_t, int32_t);
};
struct XpEpubTestRenderObservation {
  int32_t counter;
  uint8_t forced;
};
extern "C" {
void* xp_epub_test_render_create(const XpEpubTestRenderSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                 const XpAppResources*);
void xp_epub_test_render_destroy(void*);
void xp_epub_test_render_contents(void*, void*, int32_t, const int32_t*, uint8_t);
void xp_epub_test_render_observe(void*, XpEpubTestRenderObservation*);
}
