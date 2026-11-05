#pragma once
#include <xp_epub_session.h>

#include "legacy_book_fixture.h"

struct XpEpubTestSettingsOverrides {
  void* context;
  void (*initial)(void*);
  void (*remember)(void*);
  void (*discard)(void*);
  void (*paint)(void*);
  void (*status_height)(void*, int32_t);
  void (*progress_height)(void*, int32_t);
  void (*snap)(void*, uint8_t);
};
struct XpEpubTestSettingsSeed {
  void* native;
  void* section;
  void* popup;
  int32_t spine;
  int32_t page;
  int32_t cached_spine;
  int32_t cached_count;
  int32_t next_page;
  uint32_t last_turn;
  uint32_t duration;
  uint8_t orientation;
  uint8_t automatic;
  XpEpubTestSettingsOverrides hooks;
};
struct XpEpubTestSettingsObservation {
  int32_t cached_spine;
  int32_t cached_count;
  int32_t next_page;
  uint32_t last_turn;
  uint32_t duration;
  uint8_t orientation;
  uint8_t automatic;
  uint8_t has_section;
};

extern "C" {
void* xp_epub_test_settings_create_legacy_section(const XpEpubTestSettingsSeed*, const XpEpubLeaves*,
                                                  const XpAppRuntime*, const XpAppResources*,
                                                  const XpEpubLegacySectionServices*);
void xp_epub_test_settings_destroy(void*);
void xp_epub_test_settings_observe(void*, XpEpubTestSettingsObservation*);
void xp_epub_test_settings_initial(void*);
void xp_epub_test_settings_orientation(void*, uint8_t);
void xp_epub_test_settings_auto(void*, uint8_t);
void xp_epub_test_settings_text(void*);
void xp_epub_test_settings_live(void*);
void xp_epub_test_settings_popup(void*, int32_t);
}
