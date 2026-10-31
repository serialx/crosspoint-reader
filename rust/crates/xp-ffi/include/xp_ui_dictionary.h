#pragma once
#include "xp_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

// Private dictionary/selection family. All pointers borrow immutable owner
// storage for the synchronous call. No callback retains pointers or reenters a
// scene builder/renderer. The selection paint entry only calls font leaves.
typedef struct {
  int16_t x, y, width, height;
  uint16_t row;
  uint8_t pageOffset;
  int8_t characterSpacing;
  uint16_t pageWordIndex;
  uint16_t reserved;
  int32_t fontId;
  uint32_t startOffset, endOffset;
  const char* text;
  uint8_t style, paragraphStart, isRtl, discretionaryHyphen;
} XpUiSelectionWord;

typedef enum { XP_UI_SELECT_CLIP, XP_UI_SELECT_LOOKUP, XP_UI_SELECT_DEFINITION } XpUiSelectionMode;
typedef enum {
  XP_UI_SELECT_IDLE,
  XP_UI_SELECT_CANCEL,
  XP_UI_SELECT_CONFIRM,
  XP_UI_SELECT_QUEUE_FULL
} XpUiSelectionEffect;
typedef struct {
  int32_t selected, range_start, page, row_count;
  int32_t drag_x, drag_y, drag_start_x, drag_start_y, dwell_index;
  uint32_t dwell_since, last_repeat;
  uint8_t dragging, ignore_touch, moved, mode;
  uint8_t repeating, pending_count, reserved[2];
  uint8_t pending[16];
} XpUiSelectionState;
typedef struct {
  XpUiRect screen, safe;
  int16_t top, right, bottom, left;
  int16_t spacing, padding, top_padding, line_height, label_width, label_height;
  int32_t action_font;
  uint8_t touch, radius, reserved[2];
} XpUiSelectionLayout;
typedef struct {
  int32_t selected, range_start, page, offset_x, offset_y;
  uint8_t valid, reserved[3];
} XpUiSelectionPainted;
// Native classifiers preserve tap/long-press ownership. Buttons use XpUiInput.
typedef struct {
  XpUiInput input;
  uint8_t tap, pressed, held, popup, reserved[4];
} XpUiSelectionInput;
typedef struct {
  uint8_t kind, invalidate, action, reserved;
} XpUiSelectionUpdate;
// action uses ClippingResult::Action: Clip=0, Lookup=1, Bookmark=2.
void xp_ui_selection_init(XpUiSelectionState*, uint8_t mode, const XpUiSelectionWord*, uint32_t count,
                          int32_t screen_width, int32_t first, int32_t last);
XpUiSelectionUpdate xp_ui_selection_update(XpUiSelectionState*, const XpUiSelectionWord*, uint32_t count,
                                           const XpUiSelectionLayout*, const XpUiSelectionInput*);
// Buttons enqueue while RenderLock is held by painting. dequeue yields one
// original event mask; the facade routes its existing popup before update.
uint8_t xp_ui_selection_poll(XpUiSelectionState*, const XpUiInput*);
uint8_t xp_ui_selection_dequeue(XpUiSelectionState*);
XpUiSelectionUpdate xp_ui_selection_buttons(XpUiSelectionState*, const XpUiSelectionWord*, uint32_t count,
                                            uint8_t buttons);
uint8_t xp_ui_selection_view(const XpUiSelectionState*, const XpUiSelectionWord*, uint32_t count,
                             const XpUiSelectionLayout*, const XpUiSelectionPainted*, const XpUiBuffer*,
                             XpUiSelectionPainted*, XpUiScene*);
// Paints a frozen selection resource into a caller-owned packed full/band target.
// Word storage is owned by ClipSelectionActivity until exit; RenderLock prevents
// destruction/replacement. Input changes indices only, never these records.
// Font callbacks borrow one record at a time; target byte loans end before calls.
typedef struct {
  void* context;
  void (*word)(void*, const XpGlyphTarget*, const XpUiSelectionWord*, int32_t x, int32_t y);
} XpUiSelectionFonts;
uint8_t xp_ui_selection_paint(const XpUiSelectionState*, const XpUiSelectionWord*, uint32_t count,
                              const XpUiSelectionLayout*, const XpUiSelectionPainted*, const XpGlyphTarget*,
                              const XpUiSelectionFonts*, uint8_t incremental);
// Resources emitted by view: page, selected span, footer, handles and action
// text. Geometry is emitted in Rust; C++ binds pages and existing text services.
enum {
  XP_UI_DICT_PAGE = 1,
  XP_UI_DICT_SELECTION,
  XP_UI_DICT_FOOTER,
  XP_UI_DICT_HEADER,
  XP_UI_DICT_BODY,
  XP_UI_DICT_MESSAGE,
  XP_UI_DICT_LOOKUP,
  XP_UI_DICT_CLIP,
  XP_UI_DICT_BOOKMARK
};

typedef struct {
  int32_t page, pages;
  uint32_t message_time, last_repeat;
  uint16_t message;
  uint8_t message_visible, failed, lookup, history_depth, reserved[2];
} XpUiDefinitionState;
typedef struct {
  XpUiRect screen;
  int16_t top, right, bottom, left;
  int16_t top_padding, header_height, spacing, side_padding, hints_height, side_hints_width;
  uint8_t orientation, touch_controls, reserved[2];
} XpUiDefinitionLayout;
typedef enum {
  XP_UI_DEFINITION_IDLE,
  XP_UI_DEFINITION_FINISH,
  XP_UI_DEFINITION_CANCEL,
  XP_UI_DEFINITION_BACK,
  XP_UI_DEFINITION_SELECT,
  XP_UI_DEFINITION_TOUCH_SELECT
} XpUiDefinitionEffect;
XpUiEffect xp_ui_definition_update(XpUiDefinitionState*, const XpUiInput*, uint8_t tap, uint8_t long_press,
                                   uint8_t header_long_press, int32_t screen_width, uint8_t touch_controls);
XpUiRect xp_ui_definition_body(const XpUiDefinitionLayout*);
uint8_t xp_ui_definition_view(const XpUiDefinitionState*, const XpUiDefinitionLayout*, const XpUiBuffer*, XpUiScene*);

typedef struct {
  uint32_t since;
  uint16_t message;
  uint8_t pending, popup;
} XpUiLookupState;
enum {
  XP_UI_DICT_LOOKING_UP,
  XP_UI_DICT_INDEXING,
  XP_UI_DICT_NOT_FOUND,
  XP_UI_DICT_LOW_MEMORY,
  XP_UI_DICT_READ_FAILED,
  XP_UI_DICT_DECOMPRESS_ERROR,
  XP_UI_DICT_ERROR
};
void xp_ui_lookup_init(XpUiLookupState*);
uint8_t xp_ui_lookup_view(const XpUiLookupState*, XpUiRect screen, const XpUiBuffer*, XpUiScene*);
// Lookup/index codes are the existing Dictionary enums. Message codes map to
// the existing Rust-backed translations in the facade. Success returns popup=None; failures keep their 1500 ms timer.
void xp_ui_lookup_result(XpUiLookupState*, uint8_t opened, uint8_t index_result, uint8_t lookup_result, uint8_t found,
                         uint32_t now_ms);
uint8_t xp_ui_lookup_expired(const XpUiLookupState*, uint32_t now_ms);
typedef struct {
  XpUiRect screen;
  int32_t font;
  int16_t margin_x, margin_y, frame, radius, text_y;
  float top_ratio;
  uint8_t bold, inverted, reserved[2];
} XpUiDictionaryPopup;
uint8_t xp_ui_dictionary_popup(const XpUiDictionaryPopup*, const char* message, const XpUiMetrics*, const XpUiBuffer*,
                               XpUiScene*);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <stddef.h>
static_assert(sizeof(XpUiSelectionWord) == (sizeof(void*) == 4 ? 36 : 48));
static_assert(alignof(XpUiSelectionWord) == (sizeof(void*) == 4 ? 4 : 8));
static_assert(offsetof(XpUiSelectionWord, x) == 0);
static_assert(offsetof(XpUiSelectionWord, y) == 2);
static_assert(offsetof(XpUiSelectionWord, width) == 4);
static_assert(offsetof(XpUiSelectionWord, height) == 6);
static_assert(offsetof(XpUiSelectionWord, row) == 8);
static_assert(offsetof(XpUiSelectionWord, pageOffset) == 10);
static_assert(offsetof(XpUiSelectionWord, characterSpacing) == 11);
static_assert(offsetof(XpUiSelectionWord, pageWordIndex) == 12);
static_assert(offsetof(XpUiSelectionWord, reserved) == 14);
static_assert(offsetof(XpUiSelectionWord, fontId) == 16);
static_assert(offsetof(XpUiSelectionWord, startOffset) == 20);
static_assert(offsetof(XpUiSelectionWord, endOffset) == 24);
static_assert(offsetof(XpUiSelectionWord, text) == (sizeof(void*) == 4 ? 28 : 32));
static_assert(offsetof(XpUiSelectionWord, style) == (sizeof(void*) == 4 ? 32 : 40));
static_assert(offsetof(XpUiSelectionWord, paragraphStart) == (sizeof(void*) == 4 ? 33 : 41));
static_assert(offsetof(XpUiSelectionWord, isRtl) == (sizeof(void*) == 4 ? 34 : 42));
static_assert(offsetof(XpUiSelectionWord, discretionaryHyphen) == (sizeof(void*) == 4 ? 35 : 43));
static_assert(sizeof(XpUiSelectionState) == 68);
static_assert(alignof(XpUiSelectionState) == 4);
static_assert(offsetof(XpUiSelectionState, selected) == 0);
static_assert(offsetof(XpUiSelectionState, range_start) == 4);
static_assert(offsetof(XpUiSelectionState, page) == 8);
static_assert(offsetof(XpUiSelectionState, row_count) == 12);
static_assert(offsetof(XpUiSelectionState, drag_x) == 16);
static_assert(offsetof(XpUiSelectionState, drag_y) == 20);
static_assert(offsetof(XpUiSelectionState, drag_start_x) == 24);
static_assert(offsetof(XpUiSelectionState, drag_start_y) == 28);
static_assert(offsetof(XpUiSelectionState, dwell_index) == 32);
static_assert(offsetof(XpUiSelectionState, dwell_since) == 36);
static_assert(offsetof(XpUiSelectionState, last_repeat) == 40);
static_assert(offsetof(XpUiSelectionState, dragging) == 44);
static_assert(offsetof(XpUiSelectionState, ignore_touch) == 45);
static_assert(offsetof(XpUiSelectionState, moved) == 46);
static_assert(offsetof(XpUiSelectionState, mode) == 47);
static_assert(offsetof(XpUiSelectionState, repeating) == 48);
static_assert(offsetof(XpUiSelectionState, pending_count) == 49);
static_assert(offsetof(XpUiSelectionState, reserved) == 50);
static_assert(offsetof(XpUiSelectionState, pending) == 52);
static_assert(sizeof(XpUiSelectionLayout) == 44);
static_assert(alignof(XpUiSelectionLayout) == 4);
static_assert(offsetof(XpUiSelectionLayout, screen) == 0);
static_assert(offsetof(XpUiSelectionLayout, safe) == 8);
static_assert(offsetof(XpUiSelectionLayout, top) == 16);
static_assert(offsetof(XpUiSelectionLayout, right) == 18);
static_assert(offsetof(XpUiSelectionLayout, bottom) == 20);
static_assert(offsetof(XpUiSelectionLayout, left) == 22);
static_assert(offsetof(XpUiSelectionLayout, spacing) == 24);
static_assert(offsetof(XpUiSelectionLayout, padding) == 26);
static_assert(offsetof(XpUiSelectionLayout, top_padding) == 28);
static_assert(offsetof(XpUiSelectionLayout, line_height) == 30);
static_assert(offsetof(XpUiSelectionLayout, label_width) == 32);
static_assert(offsetof(XpUiSelectionLayout, label_height) == 34);
static_assert(offsetof(XpUiSelectionLayout, action_font) == 36);
static_assert(offsetof(XpUiSelectionLayout, touch) == 40);
static_assert(offsetof(XpUiSelectionLayout, radius) == 41);
static_assert(offsetof(XpUiSelectionLayout, reserved) == 42);
static_assert(sizeof(XpUiSelectionPainted) == 24);
static_assert(alignof(XpUiSelectionPainted) == 4);
static_assert(offsetof(XpUiSelectionPainted, selected) == 0);
static_assert(offsetof(XpUiSelectionPainted, range_start) == 4);
static_assert(offsetof(XpUiSelectionPainted, page) == 8);
static_assert(offsetof(XpUiSelectionPainted, offset_x) == 12);
static_assert(offsetof(XpUiSelectionPainted, offset_y) == 16);
static_assert(offsetof(XpUiSelectionPainted, valid) == 20);
static_assert(offsetof(XpUiSelectionPainted, reserved) == 21);
static_assert(sizeof(XpUiSelectionInput) == 44);
static_assert(alignof(XpUiSelectionInput) == 4);
static_assert(offsetof(XpUiSelectionInput, input) == 0);
static_assert(offsetof(XpUiSelectionInput, tap) == 36);
static_assert(offsetof(XpUiSelectionInput, pressed) == 37);
static_assert(offsetof(XpUiSelectionInput, held) == 38);
static_assert(offsetof(XpUiSelectionInput, popup) == 39);
static_assert(offsetof(XpUiSelectionInput, reserved) == 40);
static_assert(sizeof(XpUiSelectionUpdate) == 4);
static_assert(alignof(XpUiSelectionUpdate) == 1);
static_assert(offsetof(XpUiSelectionUpdate, kind) == 0);
static_assert(offsetof(XpUiSelectionUpdate, invalidate) == 1);
static_assert(offsetof(XpUiSelectionUpdate, action) == 2);
static_assert(offsetof(XpUiSelectionUpdate, reserved) == 3);
static_assert(sizeof(XpUiSelectionFonts) == (sizeof(void*) == 4 ? 8 : 16));
static_assert(alignof(XpUiSelectionFonts) == (sizeof(void*) == 4 ? 4 : 8));
static_assert(offsetof(XpUiSelectionFonts, context) == 0);
static_assert(offsetof(XpUiSelectionFonts, word) == (sizeof(void*) == 4 ? 4 : 8));
static_assert(sizeof(XpUiDefinitionState) == 24);
static_assert(alignof(XpUiDefinitionState) == 4);
static_assert(offsetof(XpUiDefinitionState, page) == 0);
static_assert(offsetof(XpUiDefinitionState, pages) == 4);
static_assert(offsetof(XpUiDefinitionState, message_time) == 8);
static_assert(offsetof(XpUiDefinitionState, last_repeat) == 12);
static_assert(offsetof(XpUiDefinitionState, message) == 16);
static_assert(offsetof(XpUiDefinitionState, message_visible) == 18);
static_assert(offsetof(XpUiDefinitionState, failed) == 19);
static_assert(offsetof(XpUiDefinitionState, lookup) == 20);
static_assert(offsetof(XpUiDefinitionState, history_depth) == 21);
static_assert(offsetof(XpUiDefinitionState, reserved) == 22);
static_assert(sizeof(XpUiDefinitionLayout) == 32);
static_assert(alignof(XpUiDefinitionLayout) == 2);
static_assert(offsetof(XpUiDefinitionLayout, screen) == 0);
static_assert(offsetof(XpUiDefinitionLayout, top) == 8);
static_assert(offsetof(XpUiDefinitionLayout, right) == 10);
static_assert(offsetof(XpUiDefinitionLayout, bottom) == 12);
static_assert(offsetof(XpUiDefinitionLayout, left) == 14);
static_assert(offsetof(XpUiDefinitionLayout, top_padding) == 16);
static_assert(offsetof(XpUiDefinitionLayout, header_height) == 18);
static_assert(offsetof(XpUiDefinitionLayout, spacing) == 20);
static_assert(offsetof(XpUiDefinitionLayout, side_padding) == 22);
static_assert(offsetof(XpUiDefinitionLayout, hints_height) == 24);
static_assert(offsetof(XpUiDefinitionLayout, side_hints_width) == 26);
static_assert(offsetof(XpUiDefinitionLayout, orientation) == 28);
static_assert(offsetof(XpUiDefinitionLayout, touch_controls) == 29);
static_assert(offsetof(XpUiDefinitionLayout, reserved) == 30);
static_assert(sizeof(XpUiLookupState) == 8);
static_assert(alignof(XpUiLookupState) == 4);
static_assert(offsetof(XpUiLookupState, since) == 0);
static_assert(offsetof(XpUiLookupState, message) == 4);
static_assert(offsetof(XpUiLookupState, pending) == 6);
static_assert(offsetof(XpUiLookupState, popup) == 7);
static_assert(sizeof(XpUiDictionaryPopup) == 32);
static_assert(alignof(XpUiDictionaryPopup) == 4);
static_assert(offsetof(XpUiDictionaryPopup, screen) == 0);
static_assert(offsetof(XpUiDictionaryPopup, font) == 8);
static_assert(offsetof(XpUiDictionaryPopup, margin_x) == 12);
static_assert(offsetof(XpUiDictionaryPopup, margin_y) == 14);
static_assert(offsetof(XpUiDictionaryPopup, frame) == 16);
static_assert(offsetof(XpUiDictionaryPopup, radius) == 18);
static_assert(offsetof(XpUiDictionaryPopup, text_y) == 20);
static_assert(offsetof(XpUiDictionaryPopup, top_ratio) == 24);
static_assert(offsetof(XpUiDictionaryPopup, bold) == 28);
static_assert(offsetof(XpUiDictionaryPopup, inverted) == 29);
static_assert(offsetof(XpUiDictionaryPopup, reserved) == 30);
#endif
