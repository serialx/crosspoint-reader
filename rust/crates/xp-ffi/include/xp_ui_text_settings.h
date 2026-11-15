#pragma once
#include "xp_ffi.h"
#include "xp_ui_reader_chrome.h"
#ifdef __cplusplus
extern "C" {
#endif

// Private text-settings family. Descriptors are aligned, disjoint and borrowed
// for the synchronous call. Resources stay immutable through scene painting.
typedef struct {
  XpUiListState tabs[4];
  uint32_t last_repeat;
  uint16_t font_count, size_count;
  int32_t family, size;
  uint8_t tab, started_on_tabs, language, reserved;
} XpUiTextState;
typedef struct {
  XpUiReaderTheme theme;
  XpUiRect bezel;
  int16_t preview_percent, preview_padding, side_padding, tab_spacing, underline;
  uint8_t full_slot, reserved;
} XpUiTextLayout;
// Field order: line, word, character, paragraph gap, indent, alignment, margin,
// focus, hyphenation, embedded style, antialiasing. Values use SETTINGS encoding.
typedef enum {
  XP_UI_TEXT_LINE,
  XP_UI_TEXT_WORD,
  XP_UI_TEXT_CHARACTER,
  XP_UI_TEXT_PARAGRAPH_GAP,
  XP_UI_TEXT_INDENT,
  XP_UI_TEXT_ALIGNMENT,
  XP_UI_TEXT_MARGIN,
  XP_UI_TEXT_FOCUS,
  XP_UI_TEXT_HYPHENATION,
  XP_UI_TEXT_EMBEDDED,
  XP_UI_TEXT_ANTIALIASING,
  XP_UI_TEXT_FIELD_COUNT
} XpUiTextField;
typedef struct {
  uint8_t fields[XP_UI_TEXT_FIELD_COUNT];
} XpUiTextValues;
typedef struct {
  int32_t value;
  uint8_t kind, field, count, selected;
} XpUiTextEdit;
enum { XP_UI_TEXT_FAMILY = 4, XP_UI_TEXT_SIZE = 5, XP_UI_TEXT_SAVE = 6, XP_UI_TEXT_OPTIONS = 7 };
#define XP_UI_TEXT_PREVIEW 0x80000020u
#define XP_UI_TEXT_TAB_BASE 0x80000030u
void xp_ui_text_init(XpUiTextState*, uint8_t tab, int32_t family, int32_t size, uint16_t fonts, uint16_t sizes,
                     uint8_t language);
XpUiInterest xp_ui_text_interest(const XpUiTextState*, uint32_t now);
XpUiEffect xp_ui_text_update(XpUiTextState*, const XpUiInput*);
void xp_ui_text_publish(XpUiTextState*, const XpUiTextState*, int32_t consumed_scroll);
// INVALID=0, OK=1, CAPACITY=2; output descriptors unchanged on failure.
uint8_t xp_ui_text_view(const XpUiTextState*, const XpUiTextLayout*, const XpUiReaderRows*, const XpUiBuffer*,
                        XpUiTextState*, XpUiScene*, XpUiReaderHitLayout*);
XpUiTextEdit xp_ui_text_edit(const XpUiTextState*, int32_t row, const XpUiTextValues*);
uint8_t xp_ui_text_choice(uint8_t field, uint8_t index);
// kind: tab=0, layout label=1, style label=2, layout value=3, option=4,
// Confirm hint=5. Numeric strings use the caller's >=16-byte scratch buffer.
// Translated labels borrow the existing generated Rust i18n tables.
const char* xp_ui_text_label(uint8_t language, uint8_t kind, uint8_t index, uint8_t value, char* scratch,
                             uint16_t capacity);

// Preview policy uses the existing paragraph/page engines and C++ resource
// owners. Feed may grow the existing paragraph storage; the other calls
// allocate nothing. No call retains the borrowed text or output pointers.
typedef struct {
  int32_t font_id, point_size, margin, width, height;
  float compression;
  uint8_t alignment, paragraph_gap, indent;
  int8_t tracking;
  uint8_t word_spacing, focus, hyphenation, reserved;
} XpUiTextPreviewKey;
typedef struct {
  XpUiRect caption, text;
  int32_t advance, gap;
  uint8_t caption_visible, text_visible, reserved[2];
} XpUiTextPreviewGeometry;
void xp_ui_text_preview_geometry(XpUiRect bounds, int16_t padding, int16_t label_gap, int32_t label_height,
                                 int32_t line_height, int32_t advance, const XpUiTextPreviewKey*,
                                 XpUiTextPreviewGeometry*);
uint8_t xp_ui_text_preview_same(const XpUiTextPreviewKey*, const XpUiTextPreviewKey*);
uint8_t xp_ui_text_preview_align(uint8_t setting);
void xp_ui_text_preview_feed(XpParagraph*, const char* text);
size_t xp_ui_text_preview_capacity(const XpParagraph*, int32_t height, int32_t line_height, int32_t advance);
// Bit 0/1 admits each of the two paragraph copies; y[0..2] is caller-owned.
uint8_t xp_ui_text_preview_line(int32_t row, int32_t lines_after, int32_t height, int32_t line_height, int32_t advance,
                                int32_t gap, int16_t* y);
#ifdef __cplusplus
}
#endif
