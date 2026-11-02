#pragma once
#include <xp_epub_session.h>
#include <xp_ui_reader.h>

struct XpEpubTestProjectionSeed {
  void* native;
  void* book;
  void* section;
  int32_t spine, page, next_page, cached_count;
  uint32_t last_turn;
  uint16_t pending_page;
  uint8_t automatic, sync_error, overlay;
  void* context;
  uint8_t (*toolbar_menu)(void*);
  uint8_t (*base_input)(void*);
  void (*acknowledge)(void*);
  void (*clear)(void*);
  void (*fallback)(void*);
  void (*estimate)(void*, int32_t);
};
struct XpEpubTestProjectionObservation {
  int32_t spine, page, next_page;
  uint32_t last_turn;
  uint16_t pending_page;
  uint8_t automatic, sync_error, has_section;
};
struct XpEpubTestChapterPosition {
  int32_t page, count;
};
extern "C" {
void* xp_epub_test_projection_create(const XpEpubTestProjectionSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                     const XpAppResources*);
void xp_epub_test_projection_destroy(void*);
void xp_epub_test_projection_observe(void*, XpEpubTestProjectionObservation*);
void xp_epub_test_projection_chapter(void*, XpEpubTestChapterPosition*);
int32_t xp_epub_test_projection_percent(void*, XpEpubTestChapterPosition);
int32_t xp_epub_test_projection_toc(void*);
void xp_epub_test_projection_screenshot(void*, XpAppScreenshot*);
int32_t xp_epub_test_projection_basis(void*);
void xp_epub_test_projection_position(void*, XpEpubPosition*);
uint32_t xp_epub_test_projection_threshold(void*);
uint8_t xp_epub_test_projection_remote(void*, uint8_t);
uint8_t xp_epub_test_projection_at_end(void*);
void xp_epub_test_projection_return_end(void*);
void xp_epub_test_projection_rendered_end(void*);
uint8_t xp_epub_test_projection_input_over(void*);
uint8_t xp_epub_test_projection_turn(void*, uint8_t);
uint8_t xp_epub_test_projection_skip(void*, int32_t);
uint8_t xp_epub_test_projection_move(void*, const XpUiReaderMove*, uint8_t);
}

struct XpEpubTestChapterDisplay {
  int32_t display_page;
  float fraction;
  uint8_t has_total;
  uint8_t reserved[3];
};
extern "C" void xp_epub_test_projection_display(void*, XpEpubTestChapterDisplay*);
