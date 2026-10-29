#include <xp_ui_text_settings.h>

#include <cstddef>
static_assert(sizeof(XpUiTextState) == 180 && alignof(XpUiTextState) == 4);
static_assert(offsetof(XpUiTextState, tabs) == 0);
static_assert(offsetof(XpUiTextState, last_repeat) == 160);
static_assert(offsetof(XpUiTextState, font_count) == 164);
static_assert(offsetof(XpUiTextState, size_count) == 166);
static_assert(offsetof(XpUiTextState, family) == 168);
static_assert(offsetof(XpUiTextState, size) == 172);
static_assert(offsetof(XpUiTextState, tab) == 176);
static_assert(offsetof(XpUiTextState, started_on_tabs) == 177);
static_assert(offsetof(XpUiTextState, language) == 178);
static_assert(offsetof(XpUiTextState, reserved) == 179);
static_assert(sizeof(XpUiTextLayout) == 92 && alignof(XpUiTextLayout) == 4);
static_assert(offsetof(XpUiTextLayout, theme) == 0);
static_assert(offsetof(XpUiTextLayout, bezel) == 72);
static_assert(offsetof(XpUiTextLayout, preview_percent) == 80);
static_assert(offsetof(XpUiTextLayout, preview_padding) == 82);
static_assert(offsetof(XpUiTextLayout, side_padding) == 84);
static_assert(offsetof(XpUiTextLayout, tab_spacing) == 86);
static_assert(offsetof(XpUiTextLayout, underline) == 88);
static_assert(offsetof(XpUiTextLayout, full_slot) == 90);
static_assert(offsetof(XpUiTextLayout, reserved) == 91);
static_assert(sizeof(XpUiTextValues) == 11 && alignof(XpUiTextValues) == 1);
static_assert(offsetof(XpUiTextValues, fields) == 0);
static_assert(sizeof(XpUiTextEdit) == 8 && alignof(XpUiTextEdit) == 4);
static_assert(offsetof(XpUiTextEdit, value) == 0);
static_assert(offsetof(XpUiTextEdit, kind) == 4);
static_assert(offsetof(XpUiTextEdit, field) == 5);
static_assert(offsetof(XpUiTextEdit, count) == 6);
static_assert(offsetof(XpUiTextEdit, selected) == 7);
static_assert(sizeof(XpUiTextPreviewKey) == 32 && alignof(XpUiTextPreviewKey) == 4);
static_assert(offsetof(XpUiTextPreviewKey, font_id) == 0);
static_assert(offsetof(XpUiTextPreviewKey, point_size) == 4);
static_assert(offsetof(XpUiTextPreviewKey, margin) == 8);
static_assert(offsetof(XpUiTextPreviewKey, width) == 12);
static_assert(offsetof(XpUiTextPreviewKey, height) == 16);
static_assert(offsetof(XpUiTextPreviewKey, compression) == 20);
static_assert(offsetof(XpUiTextPreviewKey, alignment) == 24);
static_assert(offsetof(XpUiTextPreviewKey, paragraph_gap) == 25);
static_assert(offsetof(XpUiTextPreviewKey, indent) == 26);
static_assert(offsetof(XpUiTextPreviewKey, tracking) == 27);
static_assert(offsetof(XpUiTextPreviewKey, word_spacing) == 28);
static_assert(offsetof(XpUiTextPreviewKey, focus) == 29);
static_assert(offsetof(XpUiTextPreviewKey, hyphenation) == 30);
static_assert(offsetof(XpUiTextPreviewKey, reserved) == 31);
static_assert(sizeof(XpUiTextPreviewGeometry) == 28 && alignof(XpUiTextPreviewGeometry) == 4);
static_assert(offsetof(XpUiTextPreviewGeometry, caption) == 0);
static_assert(offsetof(XpUiTextPreviewGeometry, text) == 8);
static_assert(offsetof(XpUiTextPreviewGeometry, advance) == 16);
static_assert(offsetof(XpUiTextPreviewGeometry, gap) == 20);
static_assert(offsetof(XpUiTextPreviewGeometry, caption_visible) == 24);
static_assert(offsetof(XpUiTextPreviewGeometry, text_visible) == 25);
static_assert(offsetof(XpUiTextPreviewGeometry, reserved) == 26);
static_assert(XP_UI_TEXT_FAMILY > XP_UI_SCROLL && XP_UI_TEXT_SIZE == XP_UI_TEXT_FAMILY + 1 &&
              XP_UI_TEXT_SAVE == XP_UI_TEXT_SIZE + 1 && XP_UI_TEXT_OPTIONS == XP_UI_TEXT_SAVE + 1);

int main() {}
