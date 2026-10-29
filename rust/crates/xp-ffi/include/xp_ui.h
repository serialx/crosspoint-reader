#pragma once
#include "xp_ffi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XP_UI_ABI_VERSION 2
#define XP_UI_MAX_COMMANDS 256
#define XP_UI_MAX_TEXT 4096
#define XP_UI_MAX_INTERACTIONS 64

typedef struct {
  int16_t x, y, w, h;
} XpUiRect;
typedef enum {
  XP_UI_FILL,
  XP_UI_STROKE,
  XP_UI_LINE,
  XP_UI_TEXT,
  XP_UI_BITMAP,
  XP_UI_PAGE,
  XP_UI_CLIP,
  XP_UI_TRANSLATE,
  XP_UI_POP,
  XP_UI_CLEAR,
  XP_UI_TEXT_HANDLE,
  XP_UI_TRIANGLE,
  XP_UI_HINT_TEXT
} XpUiCommandKind;
typedef struct {
  XpUiRect rects[8];
  uint8_t count, full;
} XpUiDamage;
void xp_ui_damage_add(XpUiDamage*, XpUiRect native_rect);

typedef enum { XP_UI_NONE, XP_UI_FAST, XP_UI_HALF, XP_UI_FULL } XpUiRefresh;
typedef enum { XP_UI_EFFECT_NONE, XP_UI_CANCEL, XP_UI_ACTIVATE, XP_UI_SCROLL } XpUiEffectKind;

typedef struct {
  XpUiRect rect;
  uint32_t resource, generation;
  int32_t font;
  uint16_t text_start, text_len;
  uint8_t kind, paint, radius, width;
  uint8_t style, align, max_lines, corners;
} XpUiCommand;
typedef struct {
  XpUiRect rect;
  uint32_t action;
  int32_t value;
  uint32_t generation;
  uint16_t inputs, flags;
} XpUiInteraction;
typedef struct {
  const XpUiCommand* commands;
  const uint8_t* text;
  const XpUiInteraction* interactions;
  uint32_t generation;
  uint16_t command_count, text_len, interaction_count;
  uint8_t refresh, reserved;
} XpUiScene;
// Named bits cover every MappedInputManager button, including independent axes.
typedef enum {
  XP_UI_BACK = 1u << 0,
  XP_UI_CONFIRM = 1u << 1,
  XP_UI_NAV_NEXT = 1u << 2,
  XP_UI_NAV_PREVIOUS = 1u << 3,
  XP_UI_LEFT = 1u << 4,
  XP_UI_RIGHT = 1u << 5,
  XP_UI_UP = 1u << 6,
  XP_UI_DOWN = 1u << 7,
  XP_UI_POWER = 1u << 8,
  XP_UI_PAGE_BACK = 1u << 9,
  XP_UI_PAGE_FORWARD = 1u << 10,
  XP_UI_SCREEN_LEFT = 1u << 11,
  XP_UI_SCREEN_RIGHT = 1u << 12,
  XP_UI_SCREEN_UP = 1u << 13,
  XP_UI_SCREEN_DOWN = 1u << 14
} XpUiButton;
typedef enum { XP_UI_TOUCH_PRESSED = 1, XP_UI_TOUCH_RELEASED = 2, XP_UI_TOUCH_HELD = 4 } XpUiTouch;
typedef enum { XP_UI_SWIPE_NONE, XP_UI_SWIPE_UP, XP_UI_SWIPE_DOWN, XP_UI_SWIPE_LEFT, XP_UI_SWIPE_RIGHT } XpUiSwipe;
typedef struct {
  uint32_t pressed, released, held, now_ms, held_ms;
  int16_t touch_x, touch_y;
  uint8_t touch_flags;
  int8_t swipe;
  uint16_t reserved;
  uint32_t action;
  int32_t value;
} XpUiInput;
// Rust screen policy; adapter only samples and consults this descriptor.
// Swipe interests use (1u << XpUiSwipe). Deadline arithmetic wraps in u32.
typedef struct {
  uint32_t pressed, released, held, held_after_ms, deadline_ms;
  uint8_t touch_flags, swipes, deadline_enabled, reserved;
} XpUiInterest;
uint8_t xp_ui_input_due(const XpUiInterest*, const XpUiInput*);
typedef struct {
  int32_t value;
  uint8_t kind, invalidate, reserved[2];
} XpUiEffect;
typedef struct {
  int32_t selected, top, visible, drawn, drawn_count, pending_scroll;
  int32_t active;
  uint32_t last_repeat;
  uint8_t follow, reserved[3];
  uint32_t navigation_revision;
} XpUiListState;
typedef struct {
  void* context;
  int32_t (*width)(void*, int32_t font, const uint8_t* text, uint16_t length, uint8_t style);
  int32_t (*line_height)(void*, int32_t font);
} XpUiMetrics;
typedef struct {
  XpUiRect area, screen;
  int16_t min_touch;
  int32_t font;
  int16_t row_height, row_gap, inset, padding, scroll_width;
  uint8_t radius, selection, scroll_left, bold;
} XpUiListStyle;
// XTC view resolves font/touch minima over these theme dimensions.
// list.row_height is the theme minimum; list.min_touch is derived.
typedef struct {
  XpUiRect screen, safe;
  XpUiListStyle list;
  int32_t title_font, hint_font;
  int16_t top_padding, header_height, spacing;
  uint8_t footer_style, touch;
} XpUiXtcLayout;
typedef struct {
  void* context;
  const uint8_t* (*text)(void*, uint32_t handle, uint16_t* length);
  void (*page_range)(void*, uint16_t index, uint32_t* start, uint32_t* end);
  uint16_t count;
  uint8_t present, reserved;
  uint32_t generation;
} XpUiResources;
typedef struct {
  XpUiCommand* commands;
  XpUiInteraction* hits;
  uint16_t command_capacity, hit_capacity;
} XpUiBuffer;
#define XP_UI_XTC_TITLE 0x80000001u
#define XP_UI_XTC_EMPTY 0x80000002u
#define XP_UI_XTC_FOOTER 0x80000010u

// Synchronous, serialized calls. Scene text and resources remain immutable;
// callbacks may not retain pointers, reenter UI, or change descriptors.
// Paint callbacks write pixels without a live Rust framebuffer loan.
typedef struct {
  void* context;
  void (*clear)(void*, const XpGlyphTarget*);
  void (*text)(void*, const XpGlyphTarget*, const XpUiCommand*, XpUiRect clip, const char* terminated_text);
  uint8_t (*resource_valid)(void*, uint32_t handle, uint32_t generation);
  void (*resource)(void*, const XpGlyphTarget*, const XpUiCommand*, XpUiRect clip);
} XpUiPaint;

uint8_t xp_ui_render(const XpUiScene* scene, const XpGlyphTarget* target, const XpUiPaint* paint);
// Layers may reuse local handles; each binds its own borrowed owner/services.
typedef struct {
  const XpUiScene* scene;
  const XpUiPaint* paint;
} XpUiLayer;
uint8_t xp_ui_render_layers(const XpUiLayer* layers, uint8_t count, const XpGlyphTarget*);
uint8_t xp_ui_hit(const XpUiScene* scene, int16_t x, int16_t y, uint16_t inputs, XpUiInteraction* out);
// Builds two commands borrowing the caller's terminated immutable text.
// Returns zero on failure, before changing the output commands.
uint8_t xp_ui_message_view(const XpUiMetrics* metrics, XpUiRect screen, int32_t font, uint8_t style,
                           const uint8_t* text, uint16_t text_len, XpUiCommand* commands, uint16_t capacity);

void xp_ui_list_publish(XpUiListState*, const XpUiListState* next, int32_t consumed_scroll);
XpUiInterest xp_ui_list_interest(const XpUiListState*, uint32_t now_ms);
uint8_t xp_ui_list_init(XpUiListState*, const XpUiResources*, uint32_t page);
XpUiEffect xp_ui_list_update(XpUiListState*, const XpUiInput*, uint16_t count, uint8_t present);
uint8_t xp_ui_xtc_view(const XpUiListState*, const XpUiXtcLayout*, const XpUiMetrics*, const XpUiResources*,
                       const XpUiBuffer*, XpUiListState* next_state, XpUiScene*);

#ifdef __cplusplus
}
static_assert(sizeof(XpUiCommand) == 32);
static_assert(sizeof(XpUiInteraction) == 24);
#endif
