#pragma once
#include "xp_ui.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  XP_UI_UTILITY_CONFIRMATION,
  XP_UI_UTILITY_INTERVAL,
  XP_UI_UTILITY_DETAILS,
  XP_UI_UTILITY_FRONTLIGHT,
  XP_UI_UTILITY_QR
} XpUiUtilityKind;
typedef enum {
  XP_UI_UTILITY_BRIGHTNESS = 1,
  XP_UI_UTILITY_WARMTH = 2,
  XP_UI_UTILITY_LIGHT = 4,
  XP_UI_UTILITY_SETTINGS = 8,
  XP_UI_UTILITY_CLEAN_PARENT = 16
} XpUiUtilityWrites;
typedef struct {
  int32_t value, minimum, maximum, small_step, large_step;
  uint32_t last_repeat, active_action;
  int32_t active_value;
  uint32_t clean_revision;
  uint8_t kind, selected, dragging, dismissed;
  uint8_t brightness, warmth, light_on, light_changed;
  uint8_t inverted, orientation, touch_mode, touch_restore;
  uint8_t edge_buttons, switch_owns_light, contact_held, flash_pending;
  int16_t last_drag_x;
  uint16_t touch_revision;
} XpUiUtilityState;
typedef struct {
  XpUiEffect navigation;
  uint32_t writes;
} XpUiUtilityEffect;
// Font and theme data, sampled once for a view. Geometry and state stay in Rust.
typedef struct {
  XpUiRect screen, safe;
  int32_t body_font, small_font, title_font, readout_font;
  int16_t top_padding, header_height, spacing, hints_height, home_top_padding, battery_height;
  int16_t popup_padding, popup_gap, popup_side_margin, selection_padding;
  int16_t space_sm, space_md, space_lg, row_height, min_touch;
  uint8_t popup_radius, popup_border, row_radius, selection;
  uint8_t control_radius, capsule_radius, sheet_radius, body_bold;
  uint8_t touch, light_present, has_warmth, show_cover;
  uint8_t html_description, reserved[3];
} XpUiUtilityLayout;
// Text pointers are borrowed by the synchronous view only; Rust does not retain them.
// Height uses the existing font/text-box service.
typedef struct {
  void* context;
  const char* (*text)(void*, uint32_t handle);
  int16_t (*text_height)(void*, uint32_t handle, int32_t font, int16_t width, uint8_t style, uint8_t lines);
} XpUiUtilityTexts;
typedef struct {
  XpUiRect cover, description;
  int16_t panel_bottom;
  uint16_t reserved;
  uint32_t clean_revision;
} XpUiUtilityFeedback;
// Init clamps interval values and seeds frontlight values supplied by the facade.
uint8_t xp_ui_utility_init(XpUiUtilityState*);
void xp_ui_utility_publish(XpUiUtilityState*, const XpUiUtilityState* rendered);
XpUiInterest xp_ui_utility_interest(const XpUiUtilityState*, uint32_t now_ms);
// published is null until successful painting; input routing stays in Rust.
XpUiUtilityEffect xp_ui_utility_update(XpUiUtilityState*, const XpUiInput*, const XpUiScene* published,
                                       const XpUiUtilityFeedback*);
uint8_t xp_ui_utility_view(const XpUiUtilityState*, const XpUiUtilityLayout*, const XpUiMetrics*,
                           const XpUiUtilityTexts*, const XpUiBuffer*, XpUiUtilityFeedback*, XpUiScene*);
// Only frontlight facades reference this entry, so other boards can discard its view.
uint8_t xp_ui_utility_frontlight_view(const XpUiUtilityState*, const XpUiUtilityLayout*, const XpUiMetrics*,
                                      const XpUiUtilityTexts*, const XpUiBuffer*, XpUiUtilityFeedback*, XpUiScene*);
#ifdef __cplusplus
}
#endif
