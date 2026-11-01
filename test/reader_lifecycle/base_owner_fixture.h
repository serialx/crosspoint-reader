#pragma once
#include <xp_epub_session.h>

struct XpEpubTestBaseHooks {
  void* context;
  void (*enter)(void*);
  void (*exit)(void*);
  void (*orientation)(void*);
  uint8_t (*load)(void*);
  uint8_t (*failure)(void*);
  uint8_t (*at_end)(void*);
  void (*return_end)(void*);
  void (*end_rendered)(void*);
  uint8_t (*back)(void*);
  uint8_t (*format)(void*);
  XpUiReaderTurnInput (*touch)(void*);
  XpUiReaderTurnInput (*buttons)(void*);
  uint8_t (*turn)(void*, uint8_t);
  uint8_t (*skip)(void*, int32_t);
  XpUiReaderSession (*snapshot)(void*);
  uint8_t (*worthy)(void*);
  void (*reset)(void*);
  void (*complete)(void*);
  void (*mark)(void*);
  void (*note)(void*, uint8_t, uint8_t);
  void (*force)(void*);
  int32_t (*percent)(void*);
  void (*home)(void*);
  void (*navigate)(void*, XpAppText);
};
struct XpEpubTestBaseSeed {
  void* native;
  void* book;
  void* end;
  XpAppText path;
  int32_t counter;
  uint8_t forced;
  uint8_t rendered;
  uint8_t remembered;
  uint8_t end_ready;
  XpEpubTestBaseHooks hooks;
  void (*finish)(void*);
  void (*update)(void*);
};
struct XpEpubTestBaseObservation {
  int32_t counter;
  uint8_t forced;
  uint8_t rendered;
  uint8_t remembered;
  uint8_t end_ready;
  void* end;
};
extern "C" {
void* xp_epub_test_base_create(const XpEpubTestBaseSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                               const XpAppResources*);
void xp_epub_test_base_destroy(void*);
void xp_epub_test_base_observe(void*, XpEpubTestBaseObservation*);
void xp_epub_test_base_enter(void*);
void xp_epub_test_base_exit(void*);
void xp_epub_test_base_loop(void*);
uint8_t xp_epub_test_base_render(void*, void (*paint)(void*), void*);
void xp_epub_test_base_remember(void*);
void xp_epub_test_base_clear_end(void*);
uint8_t xp_epub_test_base_end_active(void*);
uint8_t xp_epub_test_base_end_menu(void*, uint8_t);
uint8_t xp_epub_test_base_end_turn(void*, uint8_t, uint8_t);
uint8_t xp_epub_test_base_force(void*);
void xp_epub_test_base_flush(void*);
}
