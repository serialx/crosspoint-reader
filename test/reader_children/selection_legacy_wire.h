#pragma once
// Frozen native oracle wire; excluded from the firmware ABI.
#include <xp_ui_dictionary.h>
#include <xp_ui_reader_chrome.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  int32_t selected;
  uint32_t last_move_ms;
} XpUiReaderFootnoteState;
XpUiInterest xp_ui_reader_footnote_interest(const XpUiReaderFootnoteState*, uint32_t now_ms);
XpUiEffect xp_ui_reader_footnote_update(XpUiReaderFootnoteState*, const XpUiInput*, uint16_t count);
// Geometry is logical, including the original top/left clipping before the
// framebuffer snapshot service. That service retains its existing 4096 B cap.
uint8_t xp_ui_reader_footnote_view(XpUiRect screen, XpUiRect link, int16_t margin_left, int16_t margin_top,
                                   int32_t font, uint32_t number_handle, const XpUiBuffer*, XpUiRect* snapshot,
                                   XpUiScene*);

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

#ifdef __cplusplus
}
#endif
