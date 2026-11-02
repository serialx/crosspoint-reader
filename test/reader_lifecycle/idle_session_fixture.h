#pragma once
#include "overlay_actions_session_fixture.h"

struct XpEpubTestIdleSeed {
  void* native;
  void* book;
  void* popup;
  XpUiReaderIdleSnapshot sample;
};
extern "C" {
void* xp_epub_test_idle_create(const XpEpubTestIdleSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                               const XpAppResources*);
void xp_epub_test_idle_destroy(void*);
uint8_t xp_epub_test_idle(void*);
}
