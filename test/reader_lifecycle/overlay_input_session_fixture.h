#pragma once
#include "overlay_actions_session_fixture.h"

struct XpEpubTestOverlayInputHooks {
  void* context;
  void (*open)(void*, uint8_t);
  void (*close)(void*);
  void (*clear)(void*);
  void (*text)(void*);
  void (*live)(void*);
  void (*text_popup)(void*, int32_t);
  void (*more)(void*, int32_t);
  void (*popup)(void*);
  void (*width)(void*, int32_t);
  void (*height)(void*, int32_t);
};
struct XpEpubTestOverlayInputSeed {
  XpEpubTestOverlaySeed base;
  int32_t spine;
  int32_t next_page;
  uint8_t more_count;
  XpEpubTestOverlayInputHooks hooks;
  void (*push_child)(void*, void*);
};
struct XpEpubTestOverlayInputObservation {
  int32_t spine;
  int32_t next_page;
  uint8_t section;
};
extern "C" {
void* xp_epub_test_overlay_input_create(const XpEpubTestOverlayInputSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                        const XpAppResources*);
void xp_epub_test_overlay_input(void*);
void xp_epub_test_overlay_input_result(void*, const XpAppResultView*);
void xp_epub_test_overlay_input_observe(void*, XpEpubTestOverlayInputObservation*);
void xp_epub_test_overlay_input_anchor(void*, XpAppTextSink, void*);
}
