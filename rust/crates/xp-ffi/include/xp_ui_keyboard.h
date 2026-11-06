#pragma once
#include "xp_app.h"
#include "xp_ui.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  const char *label, *output, *alternate;
  int16_t value;
  uint8_t kind, units;
} XpUiKeyboardKey;
typedef struct {
  uint8_t count, inset, independent, reserved;
} XpUiKeyboardRow;
typedef struct {
  void* context;
  XpUiKeyboardRow (*row)(void*, uint8_t layout, uint8_t shifted, uint8_t symbols, uint8_t language_key, uint8_t row);
  XpUiKeyboardKey (*key)(void*, uint8_t layout, uint8_t shifted, uint8_t symbols, uint8_t language_key, uint8_t row,
                         uint8_t col);
} XpUiKeyboardLayouts;
typedef struct {
  XpUiRect screen, content;
  int32_t font, small_font;
  int16_t top_padding, header_height, spacing, vertical_offset;
  int16_t key_spacing, key_height, hints_height, side_width, text_percent;
  int16_t field_normal, field_cursor, field_end;
  uint8_t centered, touch, x3, language;
  uint16_t strings[18];
} XpUiKeyboardViewLayout;
typedef struct {
  void* context;
  // mode: 0 ink width, 1 logical advance, 2 paragraph direction (RTL=1).
  int32_t (*measure)(void*, int32_t font, const uint8_t*, uint32_t length, uint8_t mode);
  int32_t (*line_height)(void*, int32_t font);
} XpUiKeyboardMetrics;
typedef struct {
  char label[16], alternate[8];
  int16_t value;
  uint8_t kind, units, label_present, alternate_present;
} XpUiKeyboardPaintKey;
uint16_t xp_ui_keyboard_enabled(uint16_t configured, uint8_t language);

// Launch storage is borrowed until release_launch, never retained by a callback.
typedef struct {
  XpAppText title, initial;
  size_t maximum;
  uint8_t input_type;
} XpAppKeyboardLaunch;
// On acceptance the existing OwnedLegacyActivity transfers to the Rust owner.
uint8_t xp_app_keyboard_push(void* activity);
void xp_keyboard_launch(void* activity, XpAppKeyboardLaunch* out);
void xp_keyboard_release_launch(void* activity);
XpUiKeyboardLayouts xp_keyboard_layouts(void);
XpUiKeyboardMetrics xp_keyboard_metrics(void* activity);
// Optional outputs sample current theme/settings at each use.
void xp_keyboard_snapshot(void* activity, XpUiKeyboardViewLayout* layout, uint16_t* configured, uint8_t* language);
void xp_keyboard_text_lock(void* activity, uint8_t acquire);
void xp_keyboard_draw_key(void* activity, const XpUiCommand* command, const XpUiKeyboardPaintKey* key, uint8_t layout,
                          uint8_t symbols, uint8_t mode_abc);
void xp_keyboard_chrome(void* activity, const XpUiCommand* command, XpUiRect clip, XpAppText title);
// Called only after child destruction, parent publication and render unlock.
void xp_keyboard_deliver(const XpAppLegacy* legacy, void* parent, XpAppText text, uint8_t cancelled);

#ifdef __cplusplus
}
#endif
