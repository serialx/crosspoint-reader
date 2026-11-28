#pragma once
// Host-only ABI for frozen pre-owner orchestration checks.
#include <xp_ui_keyboard.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  uint32_t length, cursor, max_length, saved_cursor, right_start_cursor;
  uint32_t last_repeat, hint_time, touch_start;
  int16_t row, col, active_key;
  uint16_t enabled_layouts;
  uint8_t layout, input_type, shifted, symbols, url_panel, show_language;
  uint8_t cursor_mode, toggle_position, password_visible, hint_visible, delete_count;
  uint8_t confirm_held, confirm_long, up_held, up_long, down_held, down_long;
  uint8_t right_held, right_long, touch_held, touch_long;
  int16_t active_index;
} XpUiKeyboardState;
typedef struct {
  XpUiEffect navigation;
  uint32_t required_capacity;
  uint8_t haptics, grow, reserved[2];
} XpUiKeyboardEffect;
typedef struct {
  uint32_t start, length;
  uint8_t raw, reserved[3];
} XpUiKeyboardTextSpan;
typedef struct {
  XpUiCommand* commands;
  XpUiInteraction* hits;
  uint8_t* text;
  uint16_t command_capacity, hit_capacity, text_capacity;
  XpUiKeyboardTextSpan* spans;
  uint16_t span_capacity;
} XpUiKeyboardBuffer;
typedef struct {
  XpUiRect visual, hit;
  int16_t value;
  uint8_t selected, reserved;
} XpUiKeyboardPlacedKey;
uint8_t xp_ui_keyboard_start(uint16_t enabled, uint8_t language);
uint8_t xp_ui_keyboard_next(uint16_t enabled, uint8_t current);
void xp_ui_keyboard_init(XpUiKeyboardState*, uint32_t length, uint32_t maximum, uint8_t input_type, uint16_t enabled,
                         uint8_t language);
XpUiInterest xp_ui_keyboard_interest(const XpUiKeyboardState*);
void xp_ui_keyboard_publish(XpUiKeyboardState*, const XpUiKeyboardState* previous, const XpUiKeyboardState* next,
                            const XpUiKeyboardLayouts*);
XpUiKeyboardEffect xp_ui_keyboard_update(XpUiKeyboardState*, uint8_t* text, uint32_t capacity,
                                         const XpUiKeyboardLayouts*, const XpUiInput*, uint32_t tapped_cursor);
uint8_t xp_ui_keyboard_mask(const XpUiKeyboardState*, const uint8_t* text, uint8_t* display, uint32_t capacity);
uint8_t xp_ui_keyboard_view(const XpUiKeyboardState*, const uint8_t* text, const uint8_t* display,
                            const XpUiKeyboardLayouts*, const XpUiKeyboardViewLayout*, const XpUiKeyboardMetrics*,
                            const XpUiKeyboardBuffer*, XpUiScene*);
uint8_t xp_ui_keyboard_cursor_at(const XpUiKeyboardState*, const uint8_t* display, const XpUiKeyboardLayouts*,
                                 const XpUiKeyboardViewLayout*, const XpUiKeyboardMetrics*, int16_t x, int16_t y,
                                 uint32_t* position);
uint8_t xp_ui_keyboard_paint_key(const XpUiKeyboardState*, const XpUiKeyboardLayouts*, uint8_t row, uint8_t col,
                                 XpUiKeyboardPaintKey*);
uint8_t xp_ui_keyboard_row(const XpUiKeyboardState*, const XpUiKeyboardLayouts*, const XpUiKeyboardViewLayout*,
                           uint8_t row, XpUiKeyboardPlacedKey* out, uint8_t capacity);
uint8_t xp_ui_keyboard_hit(const XpUiKeyboardState*, const XpUiKeyboardLayouts*, const XpUiKeyboardViewLayout*,
                           int16_t x, int16_t y, XpUiInteraction* out);
#ifdef __cplusplus
}
#endif
