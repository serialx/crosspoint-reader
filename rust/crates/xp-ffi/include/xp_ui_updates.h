#pragma once
#include "xp_ui.h"
#include "xp_ui_settings.h"
#ifdef __cplusplus
extern "C" {
#endif

// Private update-screen family ABI. Descriptors are aligned, live, disjoint;
// mutable descriptors are exclusive. Borrowed resources survive synchronous paint.
typedef enum {
  XP_UI_UPDATE_OTA = 0,
  XP_UI_UPDATE_SD = 1,
  XP_UI_UPDATE_FONTS = 2,
  XP_UI_UPDATE_CACHE = 3,
} XpUiUpdateKind;
typedef enum {
  XP_UI_UPDATE_WIFI_RESULT = 0,      // a: Wi-Fi succeeded (0/1); b: 0.
  XP_UI_UPDATE_OTA_CHECKED = 1,      // a: OtaUpdaterError code; b: release is newer (0/1).
  XP_UI_UPDATE_INSTALL_BEGIN = 2,    // a, b: 0; begin the confirmed OTA installation.
  XP_UI_UPDATE_INSTALLED = 3,        // a: 0 on install success, nonzero on failure; b: 0.
  XP_UI_UPDATE_SHUTDOWN = 4,         // a, b: 0; completion screen delay has elapsed.
  XP_UI_UPDATE_PICKED = 5,           // a: picker cancelled (0/1); b: result contains a path (0/1).
  XP_UI_UPDATE_VALIDATED = 6,        // a: validation succeeded (0/1); b: 0.
  XP_UI_UPDATE_CONFIRMED = 7,        // a: confirmation cancelled (0/1); b: 0.
  XP_UI_UPDATE_PROGRESS = 8,         // a: processed bytes; b: total bytes (both uint32_t).
  XP_UI_UPDATE_MANIFEST = 9,         // a: manifest parsed (0/1); b: catalog has script groups (0/1).
  XP_UI_UPDATE_GROUP = 10,           // a, b: 0; group resource filtering is complete.
  XP_UI_UPDATE_FONT_BEGIN = 11,      // a: zero-based catalog family index <= INT32_MAX; b: 0.
  XP_UI_UPDATE_FONT_FILE = 12,       // a: expected bytes in the next font file; b: 0 (pre-transfer metadata).
  XP_UI_UPDATE_FONT_FILE_DONE = 13,  // a, b: 0; one font file passed CRC and format validation.
  XP_UI_UPDATE_FONT_DONE = 14,       // a, b: 0; the font family or batch completed.
  XP_UI_UPDATE_FONT_ERROR = 15,      // a, b: 0; the font operation failed (text remains a borrowed resource).
  XP_UI_UPDATE_FONT_ABORTED = 16,    // a, b: 0; the transfer abort and partial-family removal completed.
  XP_UI_UPDATE_TRANSFER_INPUT = 17,  // a: Back held/pressed (0/1); b: Home gesture (0/1).
  XP_UI_UPDATE_CACHE_BEGIN = 18,     // a, b: 0; cache deletion was confirmed.
  XP_UI_UPDATE_CACHE_RESULT = 19,    // a: cache directory opened (0/1); b: 0 (per-entry failures are result data).
  XP_UI_UPDATE_FONT_BATCH = 20,      // a: number of files in the chosen family/batch; b: 0.
  XP_UI_UPDATE_SCENE_FAILED = 21,    // a, b: 0; build or paint rejected, block activation until publication.
} XpUiUpdateEvent;
typedef enum {
  XP_UI_UPDATE_NONE = 0,            // value: 0.
  XP_UI_UPDATE_FINISH = 1,          // value: 0.
  XP_UI_UPDATE_PICKER = 2,          // value: 0.
  XP_UI_UPDATE_INSTALL = 5,         // value: 0.
  XP_UI_UPDATE_FONT_RETRY = 9,      // value: zero-based catalog family index.
  XP_UI_UPDATE_FONT_ACTIVATE = 10,  // value: zero-based visible family-list row.
  XP_UI_UPDATE_ENTER_GROUP = 11,    // value: zero-based visible group-list row.
  XP_UI_UPDATE_CLEAR = 12,          // value: 0.
  XP_UI_UPDATE_RESTART = 13,        // value: 0.
  XP_UI_UPDATE_HOME = 14,           // value: 0.
} XpUiUpdateAction;
typedef struct {
  XpUiListState list, groups;
  uint32_t processed, total, file_index, file_count, last_percent;
  int32_t family_index;
  uint8_t kind, phase, recovery, cancel_requested, home_requested;
  uint8_t has_groups, popup_selected, popup_active, touch_active;
  uint8_t scene_failed, reserved[2];
} XpUiUpdateState;
typedef struct {
  uint8_t action;
  // repaint: 0 none, 1 after action, 2 before action. immediate: notify render task.
  uint8_t repaint, immediate, reserved;
  // Zero except FontRetry/FontActivate/EnterGroup: zero-based family/row/group index.
  int32_t value;
} XpUiUpdateEffect;
typedef struct {
  XpUiSettingsLayout list;
  int16_t progress_height, popup_padding, popup_gap, popup_side_margin, selection_padding;
  uint8_t popup_radius, popup_border, reserved[2];
} XpUiUpdateLayout;
typedef struct {
  XpUiSettingsResources rows;
  void* context;
  const char* (*text)(void* context, uint32_t handle);
  int16_t (*text_height)(void* context, uint32_t handle, int32_t font, int16_t width, uint8_t bold, uint8_t max_lines);
} XpUiUpdateResources;
typedef struct {
  XpUiListState list;
  XpUiSettingsHits rows;
  XpUiRect popup, options[2];
  uint32_t last_percent;
  uint8_t rendered, reserved[3];
} XpUiUpdateFeedback;
void xp_ui_updates_init(XpUiUpdateState* state, uint8_t kind, uint8_t recovery);
// Notifications change UI state only; C++ owns synchronous service and repaint ordering.
void xp_ui_updates_notify(XpUiUpdateState* state, uint8_t event, uint32_t a, uint32_t b);
// Results: WifiResult -> None/Finish; Picked -> None/Finish/Picker;
// Confirmed -> None/Finish/Picker; FontAborted -> None/Home (XpUiUpdateAction).
uint8_t xp_ui_updates_result(XpUiUpdateState* state, uint8_t event, uint32_t a, uint32_t b);
XpUiInterest xp_ui_updates_interest(const XpUiUpdateState* state, uint32_t now_ms);
XpUiUpdateEffect xp_ui_updates_input(XpUiUpdateState* state, const XpUiInput* input, const XpUiUpdateFeedback* feedback,
                                     uint8_t published, int32_t row_count, int32_t family_count);
uint8_t xp_ui_updates_sleep(const XpUiUpdateState* state);
uint8_t xp_ui_updates_delay(const XpUiUpdateState* state);
uint32_t xp_ui_updates_percent(const XpUiUpdateState* state);
uint8_t xp_ui_updates_view(const XpUiUpdateState* state, const XpUiUpdateLayout* layout,
                           const XpUiUpdateResources* resources, const XpUiBuffer* buffer, XpUiUpdateFeedback* feedback,
                           XpUiScene* scene);
void xp_ui_updates_publish(XpUiUpdateState* state, const XpUiUpdateState* snapshot, const XpUiUpdateFeedback* feedback);
#ifdef __cplusplus
}
#endif

#include <stddef.h>
#ifdef __cplusplus
#define XP_UPDATE_ASSERT static_assert
#define XP_UPDATE_ALIGN(T) alignof(T)
#else
#define XP_UPDATE_ASSERT _Static_assert
#define XP_UPDATE_ALIGN(T) _Alignof(T)
#endif
XP_UPDATE_ASSERT(sizeof(XpUiUpdateState) == 116, "XpUiUpdateState size");
XP_UPDATE_ASSERT(XP_UPDATE_ALIGN(XpUiUpdateState) == 4, "XpUiUpdateState alignment");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, list) == 0, "XpUiUpdateState.list");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, groups) == 40, "XpUiUpdateState.groups");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, processed) == 80, "XpUiUpdateState.processed");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, total) == 84, "XpUiUpdateState.total");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, file_index) == 88, "XpUiUpdateState.file_index");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, file_count) == 92, "XpUiUpdateState.file_count");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, last_percent) == 96, "XpUiUpdateState.last_percent");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, family_index) == 100, "XpUiUpdateState.family_index");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, kind) == 104, "XpUiUpdateState.kind");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, phase) == 105, "XpUiUpdateState.phase");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, recovery) == 106, "XpUiUpdateState.recovery");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, cancel_requested) == 107, "XpUiUpdateState.cancel_requested");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, home_requested) == 108, "XpUiUpdateState.home_requested");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, has_groups) == 109, "XpUiUpdateState.has_groups");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, popup_selected) == 110, "XpUiUpdateState.popup_selected");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, popup_active) == 111, "XpUiUpdateState.popup_active");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, touch_active) == 112, "XpUiUpdateState.touch_active");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, scene_failed) == 113, "XpUiUpdateState.scene_failed");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateState, reserved) == 114, "XpUiUpdateState.reserved");
XP_UPDATE_ASSERT(sizeof(XpUiUpdateEffect) == 8, "XpUiUpdateEffect size");
XP_UPDATE_ASSERT(XP_UPDATE_ALIGN(XpUiUpdateEffect) == 4, "XpUiUpdateEffect alignment");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateEffect, action) == 0, "XpUiUpdateEffect.action");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateEffect, repaint) == 1, "XpUiUpdateEffect.repaint");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateEffect, immediate) == 2, "XpUiUpdateEffect.immediate");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateEffect, reserved) == 3, "XpUiUpdateEffect.reserved");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateEffect, value) == 4, "XpUiUpdateEffect.value");
XP_UPDATE_ASSERT(sizeof(XpUiUpdateLayout) == 96, "XpUiUpdateLayout size");
XP_UPDATE_ASSERT(XP_UPDATE_ALIGN(XpUiUpdateLayout) == 4, "XpUiUpdateLayout alignment");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateLayout, list) == 0, "XpUiUpdateLayout.list");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateLayout, progress_height) == 80, "XpUiUpdateLayout.progress_height");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateLayout, popup_padding) == 82, "XpUiUpdateLayout.popup_padding");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateLayout, popup_gap) == 84, "XpUiUpdateLayout.popup_gap");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateLayout, popup_side_margin) == 86, "XpUiUpdateLayout.popup_side_margin");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateLayout, selection_padding) == 88, "XpUiUpdateLayout.selection_padding");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateLayout, popup_radius) == 90, "XpUiUpdateLayout.popup_radius");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateLayout, popup_border) == 91, "XpUiUpdateLayout.popup_border");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateLayout, reserved) == 92, "XpUiUpdateLayout.reserved");
XP_UPDATE_ASSERT(sizeof(XpUiUpdateResources) == 9 * sizeof(void*), "XpUiUpdateResources size");
XP_UPDATE_ASSERT(XP_UPDATE_ALIGN(XpUiUpdateResources) == XP_UPDATE_ALIGN(void*), "XpUiUpdateResources alignment");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateResources, rows) == 0, "XpUiUpdateResources.rows");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateResources, context) == 6 * sizeof(void*), "XpUiUpdateResources.context");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateResources, text) == 7 * sizeof(void*), "XpUiUpdateResources.text");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateResources, text_height) == 8 * sizeof(void*), "XpUiUpdateResources.text_height");
XP_UPDATE_ASSERT(sizeof(XpUiUpdateFeedback) == 156, "XpUiUpdateFeedback size");
XP_UPDATE_ASSERT(XP_UPDATE_ALIGN(XpUiUpdateFeedback) == 4, "XpUiUpdateFeedback alignment");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateFeedback, list) == 0, "XpUiUpdateFeedback.list");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateFeedback, rows) == 40, "XpUiUpdateFeedback.rows");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateFeedback, popup) == 124, "XpUiUpdateFeedback.popup");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateFeedback, options) == 132, "XpUiUpdateFeedback.options");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateFeedback, last_percent) == 148, "XpUiUpdateFeedback.last_percent");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateFeedback, rendered) == 152, "XpUiUpdateFeedback.rendered");
XP_UPDATE_ASSERT(offsetof(XpUiUpdateFeedback, reserved) == 153, "XpUiUpdateFeedback.reserved");
#undef XP_UPDATE_ALIGN
#undef XP_UPDATE_ASSERT
