#pragma once
#include "overlay_actions_session_fixture.h"

struct XpEpubTestLoopHooks {
  void* context;
  uint8_t (*idle)(void*);
  void (*remember)(void*);
  void (*clear_end)(void*);
  uint8_t (*touch_menu)(void*);
  void (*overlay_input)(void*);
  void (*restore)(void*);
  void (*background)(void*);
  uint8_t (*end_menu)(void*);
  uint8_t (*end_active)(void*);
  uint32_t (*threshold)(void*);
  uint8_t (*end_turn)(void*, uint8_t, uint8_t);
  uint8_t (*sync)(void*);
  uint8_t (*link_repaint)(void*, uint8_t);
  int32_t (*link_hit)(void*, int32_t, int32_t, int32_t, int32_t);
  void (*framebuffer)(void*, uint8_t);
  void (*font_cache)(void*, uint8_t);
  void (*read_folder)(void*, uint8_t);
  void (*orientation)(void*, uint8_t);
};
struct XpEpubTestLoopState {
  int32_t spine, page, idle_spine, idle_page;
  uint32_t last_render, last_turn, duration;
  uint32_t bookmark_time, dictionary_time, font_time, clipping_time;
  int32_t link_left, link_top;
  uint16_t width, height;
  uint8_t orientation, radio_released, partial_failed, recents_removed, read_move, overlay, automatic;
  int8_t manual_turn;
  uint8_t manual_touch, bookmark, dictionary, font, clipping, history_depth;
};
struct XpEpubTestLoopSeed {
  void* native;
  void* book;
  void* section;
  void* popup;
  XpEpubTestLoopState state;
  XpEpubTestLoopHooks hooks;
  void (*finish)(void*);
  void (*update)(void*);
  XpUiReaderTurnInput (*buttons)(void*);
  XpUiReaderTurnInput (*touch)(void*);
  uint8_t (*back)(void*);
  uint8_t (*turn)(void*, uint8_t);
  uint8_t (*skip)(void*, int32_t);
  void (*note)(void*, uint8_t, uint8_t);
  uint8_t (*toolbar)(void*);
  void (*bookmark)(void*);
  void (*dictionary)(void*);
  void (*footnotes)(void*, uint8_t);
  void (*selection)(void*, int32_t, int32_t, uint8_t);
  void (*menu)(void*);
  void (*navigate)(void*, XpAppText, uint8_t);
  void (*overlay)(void*, uint8_t);
  void (*discard)(void*);
};
struct XpEpubTestLoopObservation {
  int32_t radio_released, idle_spine, idle_page, partial_failed, recents_removed, read_move;
  int32_t overlay;
  uint32_t last_turn;
  int32_t automatic, manual_turn, manual_touch, bookmark, dictionary, font, clipping, orientation, spine;
};
extern "C" {
void* xp_epub_test_loop_create(const XpEpubTestLoopSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                               const XpAppResources*);
void xp_epub_test_loop(void*);
void xp_epub_test_loop_observe(void*, XpEpubTestLoopObservation*);
void xp_epub_test_loop_destroy(void*);
}
