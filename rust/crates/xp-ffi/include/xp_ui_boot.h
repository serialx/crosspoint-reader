#pragma once
#include "xp_ui.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  XP_UI_BOOT_LOGO = 1,
  XP_UI_BOOT_MOON_ICON,
  XP_UI_BOOT_HEADER,
  XP_UI_BOOT_HINTS,
  XP_UI_BOOT_VERSION,
  XP_UI_BOOT_TITLE,
  XP_UI_BOOT_SUBTITLE,
  XP_UI_BOOT_REASON,
  XP_UI_BOOT_ERROR,
  XP_UI_BOOT_IMAGE_BUTTONS,
  XP_UI_BOOT_FIRST_HINT,
  XP_UI_BOOT_XTC_PIXELS,
  XP_UI_BOOT_DESCRIPTION = 16,
  XP_UI_BOOT_PANIC = 32
} XpUiBootResource;
typedef enum { XP_UI_BOOT_NO_HINTS = 1, XP_UI_BOOT_ONLY_FIRST_HINT = 2, XP_UI_BOOT_RETAIN = 4 } XpUiBootFlags;

// Private boot/sleep/crash/image/XTC family. Descriptors are borrowed only for the call.
typedef enum {
  XP_UI_BOOT_SPLASH,
  XP_UI_BOOT_SLEEP,
  XP_UI_BOOT_BLANK,
  XP_UI_BOOT_MOON,
  XP_UI_BOOT_CRASH,
  XP_UI_BOOT_IMAGE_ERROR,
  XP_UI_BOOT_PAGE_ERROR,
  XP_UI_BOOT_IMAGE_HINTS,
  XP_UI_BOOT_XTC_PAGE
} XpUiBootView;
typedef enum {
  XP_UI_SLEEP_DEFAULT,
  XP_UI_SLEEP_CUSTOM,
  XP_UI_SLEEP_COVER,
  XP_UI_SLEEP_OVERLAY,
  XP_UI_SLEEP_BLANK,
  XP_UI_SLEEP_QUICK
} XpUiSleepRoute;
typedef struct {
  uint8_t route, fallback, preserve_frame, invert_frame;
  uint8_t normalize_polarity, dark_default, release_fonts, overlay_cover;
} XpUiSleepPlan;
typedef struct {
  XpUiRect screen;
  int32_t title_font, small_font, error_font;
  int16_t side_padding, top_padding, header_height, spacing, line_height;
  uint8_t view, refresh;
  // Error flags: 1=no hints, 2=first mapped hint only, 4=retain background.
  uint16_t flags;
  uint8_t description_lines, panic_lines;
} XpUiBootLayout;
// Fixed resources: logo=1, moon=2, header=3, hints=4, panic lines=32..36.
// Text resources: version=5, title=6, subtitle=7, reason=8, error=9.
// Description lines=16..25; all strings use the existing Rust i18n tables.
typedef struct {
  void* context;
  int32_t (*width)(void*, uint32_t handle, int32_t font, uint8_t style);
} XpUiBootMetrics;
typedef struct {
  int32_t selected, count;
  uint8_t can_set_cover, reserved[3];
} XpUiImageState;
typedef struct {
  uint32_t page;
} XpUiXtcState;
typedef struct {
  XpUiRect clear;
  int32_t padding_bottom;
  uint8_t draw, reserved[3];
} XpUiXtcOverlay;
typedef struct {
  int32_t x, y;
  float crop_x, crop_y;
} XpUiImagePlacement;

XpUiSleepPlan xp_ui_sleep_plan(uint8_t mode, uint8_t timeout, uint8_t quick_after_timeout, uint8_t from_reader,
                               uint8_t inverted);
uint8_t xp_ui_boot_view(const XpUiBootLayout*, const XpUiBootMetrics*, const XpUiBuffer*, XpUiScene*);
XpUiInterest xp_ui_boot_interest(uint8_t crash);
// Crash input uses action=1 for the platform's completed screen-tap gesture.
XpUiEffect xp_ui_crash_update(const XpUiInput*);
// Image update returns Cancel (browser), Activate (cover confirmation), or Scroll (new sibling).
XpUiEffect xp_ui_image_update(XpUiImageState*, const XpUiInput*);
uint8_t xp_ui_image_hints(const XpUiImageState*);
XpUiXtcOverlay xp_ui_xtc_overlay(int16_t screen_width, int16_t screen_height, int16_t top, int16_t bottom,
                                 int16_t height, uint8_t mode, uint8_t position);
uint8_t xp_ui_xtc_turn(XpUiXtcState*, uint32_t count, int32_t amount, uint8_t skip);
void xp_ui_xtc_restore(XpUiXtcState*, uint32_t count, uint8_t from_end);
XpUiImagePlacement xp_ui_image_place(int32_t width, int32_t height, int32_t screen_width, int32_t screen_height,
                                     uint8_t crop);
// Called only for a decoded pixel outside the physical panel; preserves renderer diagnostics.
// Synchronous, no retained pointers; callback may inspect the renderer but must not mutate the target.
typedef struct {
  void* context;
  void (*outside)(void*, int32_t x, int32_t y, uint8_t black);
} XpUiXtcDiagnostics;
// The target buffer and page bytes are disjoint; no pointer is retained.
uint8_t xp_ui_xtc_pixels(const XpGlyphTarget*, const uint8_t* bytes, uint32_t length, uint16_t width, uint16_t height,
                         uint8_t depth, uint8_t pass, const XpUiXtcDiagnostics*);
#ifdef __cplusplus
}
#endif
