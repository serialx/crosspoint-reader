#pragma once
#include <xp_epub_session.h>
#include <xp_ui_reader.h>

struct XpEpubPublicationSeed {
  void* book;
  XpUiReaderSession session;
  int32_t refresh_counter;
  uint8_t ready, remembered;
};
struct XpEpubPublicationObservation {
  XpUiReaderSession session;
  int32_t refresh_counter;
  uint8_t refresh_requests, end_ready, rendered, remembered;
};
extern "C" {
void* xp_epub_test_publication_create(const XpEpubPublicationSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                      const XpAppResources*);
void xp_epub_test_publication_destroy(void*);
void xp_epub_test_publication_observe(void*, XpEpubPublicationObservation*);
void xp_epub_test_publication_ready(void*, uint8_t);
void xp_epub_test_publication_remember(void*);
void xp_epub_test_publication_note(void*, uint8_t, uint8_t);
void xp_epub_test_publication_force(void*);
void xp_epub_test_publication_disable_fast(void*);
void xp_epub_test_publication_counter(void*, int32_t);
uint8_t xp_epub_test_publication_accepts(void*);
void xp_epub_test_publication_reset(void*);
// The caller already owns RenderLock. The frame token expires when paint returns.
uint8_t xp_epub_test_publication_render(void*, void (*paint)(void*, void*), void* context);
uint8_t xp_epub_test_publication_frame_present(void*);
void xp_epub_test_publication_frame_observe(void*, XpEpubPublicationObservation*);
int32_t xp_epub_test_publication_frame_counter(void*);
void xp_epub_test_publication_frame_set_counter(void*, int32_t);
uint8_t xp_epub_test_publication_frame_take_force(void*);
void xp_epub_test_publication_frame_mark(void*);
void xp_epub_test_publication_frame_complete(void*);
// Wire conversion only; each operation invokes the ordinary production type.
void xp_epub_test_accounting_note(XpUiReaderSession*, uint8_t, uint8_t);
void xp_epub_test_accounting_render(XpUiReaderSession*, uint32_t, int64_t, int32_t);
uint8_t xp_epub_test_accounting_worthy(const XpUiReaderSession*);
}
