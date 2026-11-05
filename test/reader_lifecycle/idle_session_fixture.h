#pragma once
#include <cstddef>

#include "legacy_book_fixture.h"
#include "overlay_actions_session_fixture.h"

// Host-only mirror of xp_ui::reader::idle::Snapshot; no policy lives here.
typedef struct {
  int32_t spine, spine_count;
  uint32_t confirm_threshold;
  uint8_t busy, has_epub, load_popup, remembered, end_options, touch, remote, home_action;
  uint8_t edges, held_buttons, overlay, automatic;
  int8_t pending_turn;
  uint8_t bookmark_notice, dictionary_notice, font_notice, clipping_notice, read_move, recents_removed;
  uint8_t applied_orientation, orientation, long_button, short_power, tilt, footnote_depth, touch_controls;
  uint8_t reserved[2];
} XpEpubTestIdleSnapshot;

static_assert(sizeof(XpEpubTestIdleSnapshot) == 40 && alignof(XpEpubTestIdleSnapshot) == 4);
static_assert(offsetof(XpEpubTestIdleSnapshot, spine) == 0);
static_assert(offsetof(XpEpubTestIdleSnapshot, spine_count) == 4);
static_assert(offsetof(XpEpubTestIdleSnapshot, confirm_threshold) == 8);
static_assert(offsetof(XpEpubTestIdleSnapshot, busy) == 12);
static_assert(offsetof(XpEpubTestIdleSnapshot, has_epub) == 13);
static_assert(offsetof(XpEpubTestIdleSnapshot, load_popup) == 14);
static_assert(offsetof(XpEpubTestIdleSnapshot, remembered) == 15);
static_assert(offsetof(XpEpubTestIdleSnapshot, end_options) == 16);
static_assert(offsetof(XpEpubTestIdleSnapshot, touch) == 17);
static_assert(offsetof(XpEpubTestIdleSnapshot, remote) == 18);
static_assert(offsetof(XpEpubTestIdleSnapshot, home_action) == 19);
static_assert(offsetof(XpEpubTestIdleSnapshot, edges) == 20);
static_assert(offsetof(XpEpubTestIdleSnapshot, held_buttons) == 21);
static_assert(offsetof(XpEpubTestIdleSnapshot, overlay) == 22);
static_assert(offsetof(XpEpubTestIdleSnapshot, automatic) == 23);
static_assert(offsetof(XpEpubTestIdleSnapshot, pending_turn) == 24);
static_assert(offsetof(XpEpubTestIdleSnapshot, bookmark_notice) == 25);
static_assert(offsetof(XpEpubTestIdleSnapshot, dictionary_notice) == 26);
static_assert(offsetof(XpEpubTestIdleSnapshot, font_notice) == 27);
static_assert(offsetof(XpEpubTestIdleSnapshot, clipping_notice) == 28);
static_assert(offsetof(XpEpubTestIdleSnapshot, read_move) == 29);
static_assert(offsetof(XpEpubTestIdleSnapshot, recents_removed) == 30);
static_assert(offsetof(XpEpubTestIdleSnapshot, applied_orientation) == 31);
static_assert(offsetof(XpEpubTestIdleSnapshot, orientation) == 32);
static_assert(offsetof(XpEpubTestIdleSnapshot, long_button) == 33);
static_assert(offsetof(XpEpubTestIdleSnapshot, short_power) == 34);
static_assert(offsetof(XpEpubTestIdleSnapshot, tilt) == 35);
static_assert(offsetof(XpEpubTestIdleSnapshot, footnote_depth) == 36);
static_assert(offsetof(XpEpubTestIdleSnapshot, touch_controls) == 37);
static_assert(offsetof(XpEpubTestIdleSnapshot, reserved) == 38);

struct XpEpubTestIdleSeed {
  void* native;
  void* book;
  void* popup;
  XpEpubTestIdleSnapshot sample;
};
extern "C" {
void* xp_epub_test_idle_create_legacy_book(const XpEpubTestIdleSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                           const XpAppResources*, const XpEpubLegacyBookServices*);
void xp_epub_test_idle_destroy(void*);
uint8_t xp_epub_test_idle(void*);
}

static_assert(offsetof(XpEpubTestIdleSeed, sample) == 3 * sizeof(void*));
static_assert(sizeof(XpEpubTestIdleSeed) == 3 * sizeof(void*) + 40);
