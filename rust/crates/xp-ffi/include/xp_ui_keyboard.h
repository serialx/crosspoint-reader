#pragma once
#include "xp_ui.h"
#ifdef __cplusplus
extern "C" {
#endif
// Keyboard-only additions; the shared UI ABI and scheduler are unchanged.
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
  const char *label, *output, *alternate;
  int16_t value;
  uint8_t kind, units;
} XpUiKeyboardKey;
typedef struct {
  uint8_t count, inset, independent, reserved;
} XpUiKeyboardRow;
// Layout callback strings are immutable through the enclosing Rust call; no
// returned pointer is retained in model state or a published scene.
typedef struct {
  void* context;
  XpUiKeyboardRow (*row)(void*, uint8_t layout, uint8_t shifted, uint8_t symbols, uint8_t language_key, uint8_t row);
  XpUiKeyboardKey (*key)(void*, uint8_t layout, uint8_t shifted, uint8_t symbols, uint8_t language_key, uint8_t row,
                         uint8_t col);
} XpUiKeyboardLayouts;
typedef struct {
  XpUiEffect navigation;
  uint32_t required_capacity;
  uint8_t haptics, grow, reserved[2];
} XpUiKeyboardEffect;
uint16_t xp_ui_keyboard_enabled(uint16_t configured, uint8_t language);
uint8_t xp_ui_keyboard_start(uint16_t enabled, uint8_t language);
uint8_t xp_ui_keyboard_next(uint16_t enabled, uint8_t current);
void xp_ui_keyboard_init(XpUiKeyboardState*, uint32_t length, uint32_t maximum, uint8_t input_type, uint16_t enabled,
                         uint8_t language);
XpUiInterest xp_ui_keyboard_interest(const XpUiKeyboardState*);
// After paint, reconcile the live active key by its interaction index across
// the old and new publications. Called under ModelLock; no allocation or paint.
void xp_ui_keyboard_publish(XpUiKeyboardState*, const XpUiKeyboardState* previous, const XpUiKeyboardState* next,
                            const XpUiKeyboardLayouts*);
// Storage is caller-owned; no allocation, I/O, logging or rendering occurs here.
// capacity includes the terminator; insufficient capacity leaves an insert unapplied.
// effect.haptics requests haptics (1 tap, 2 hold). grow==1 reports
// required byte capacity in effect.required_capacity; facade grows outside ModelLock, restores
// its saved state, then retries the same input. On allocation failure it logs once
// and retains the attempted no-op state; text/cursor/result remain unchanged.
XpUiKeyboardEffect xp_ui_keyboard_update(XpUiKeyboardState*, uint8_t* text, uint32_t capacity,
                                         const XpUiKeyboardLayouts*, const XpUiInput*, uint32_t tapped_cursor);
#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
extern "C" {
#endif
// Resource IDs 1/2/3 bind the existing shared theme header, front hints and side
// hints. Text handles 0x200+index bind validated immutable snapshot spans.
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
// All byte slices and resource bindings stay immutable through painting/strips.
// Keyboard scenes publish no hit records; hit queries use the painted state.
// Failure does not publish a partial scene.
uint8_t xp_ui_keyboard_mask(const XpUiKeyboardState*, const uint8_t* text, uint8_t* display, uint32_t capacity);
uint8_t xp_ui_keyboard_view(const XpUiKeyboardState*, const uint8_t* text, const uint8_t* display,
                            const XpUiKeyboardLayouts*, const XpUiKeyboardViewLayout*, const XpUiKeyboardMetrics*,
                            const XpUiKeyboardBuffer*, XpUiScene*);
// Called on a private text snapshot outside ModelLock; font measurement is not
// performed by xp_ui_keyboard_update or any other model-lock hook.
uint8_t xp_ui_keyboard_cursor_at(const XpUiKeyboardState*, const uint8_t* display, const XpUiKeyboardLayouts*,
                                 const XpUiKeyboardViewLayout*, const XpUiKeyboardMetrics*, int16_t x, int16_t y,
                                 uint32_t* position);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
// A bounded copy for the SDK's single-key pixel painter. Rust keeps ownership
// of layout selection, key placement, hit rectangles and semantic activation.
typedef struct {
  char label[16], alternate[8];
  int16_t value;
  uint8_t kind, units, label_present, alternate_present;
} XpUiKeyboardPaintKey;
uint8_t xp_ui_keyboard_paint_key(const XpUiKeyboardState*, const XpUiKeyboardLayouts*, uint8_t row, uint8_t col,
                                 XpUiKeyboardPaintKey*);
// View resource IDs 0x100..0x104 bind row painters. Each row uses Rust key
// geometry and the SDK single-key painter, without registering hit records.

typedef struct {
  XpUiRect visual, hit;
  int16_t value;
  uint8_t selected, reserved;
} XpUiKeyboardPlacedKey;
// Row output is scratch; a zero result means failure and must not be painted.
uint8_t xp_ui_keyboard_row(const XpUiKeyboardState*, const XpUiKeyboardLayouts*, const XpUiKeyboardViewLayout*,
                           uint8_t row, XpUiKeyboardPlacedKey* out, uint8_t capacity);
// Use the immutable painted snapshot, not the live model. Failure leaves out unchanged.
uint8_t xp_ui_keyboard_hit(const XpUiKeyboardState*, const XpUiKeyboardLayouts*, const XpUiKeyboardViewLayout*,
                           int16_t x, int16_t y, XpUiInteraction* out);

#ifdef __cplusplus
}
#endif
