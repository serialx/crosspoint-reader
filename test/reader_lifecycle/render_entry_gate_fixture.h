#pragma once
#include <xp_epub_session.h>

struct XpEpubEntryGateObservation {
  uint32_t anchor_ms;
  uint32_t duration;
  int32_t refresh_counter;
  uint8_t ready;
  uint8_t frame_present;
  uint8_t rendered;
  uint8_t scene_failed;
  uint8_t refresh_requests;
  uint8_t pending_forward;
};
extern "C" {
void* xp_epub_test_entry_gate_create(void* native, void* popup, const XpEpubLeaves*, const XpAppRuntime*,
                                     const XpAppResources*);
void xp_epub_test_entry_gate_destroy(void*);
uint8_t xp_epub_test_entry_gate_load(void*);
uint8_t xp_epub_test_entry_gate_render(void*);
void xp_epub_test_entry_gate_observe(void*, XpEpubEntryGateObservation*);
}
