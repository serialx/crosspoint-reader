#pragma once
#include <xp_epub_session.h>

#include "legacy_page_fixture.h"

struct XpEpubTestRecoverySeed {
  void* native;
  void* popup;
  XpAppText path;
  uint8_t failure;
  void* context;
  void (*finish)(void*);
  void (*push)(void*, void*);
  void (*reopen)(void*, XpAppText);
  void (*update)(void*);
};
extern "C" {
void* xp_epub_test_recovery_create_legacy_render(const XpEpubTestRecoverySeed*, const XpEpubLeaves*,
                                                 const XpAppRuntime*, const XpAppResources*,
                                                 const XpEpubLegacyPageServices* = nullptr);
void xp_epub_test_recovery_destroy(void*);
void xp_epub_test_recovery_seed_failure(void*, uint8_t);
uint8_t xp_epub_test_recovery_seed_path(void*, XpAppText);
uint8_t xp_epub_test_recovery_handle(void*);
void xp_epub_test_recovery_begin(void*);
void xp_epub_test_recovery_wifi_result(void*, uint8_t);
uint8_t xp_epub_test_recovery_render(void*);
}
