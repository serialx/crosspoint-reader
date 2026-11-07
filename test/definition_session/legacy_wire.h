#pragma once
// Wire retained only for frozen native reference harnesses.
#include <xp_app.h>
#include <xp_ui_dictionary.h>
// Historical transfer wire with its allocator-matched release callback.
typedef void (*XpDefinitionTextRelease)(uint8_t* data, uint32_t capacity);
typedef struct {
  uint8_t* data;
  uint32_t len, capacity;
  XpDefinitionTextRelease release;
} XpDefinitionOwnedText;
enum {
  XP_DEFINITION_SELECTION_CANCELLED,
  XP_DEFINITION_SELECTION_SELECTED,
  XP_DEFINITION_SELECTION_DEPTH_FULL,
  XP_DEFINITION_SELECTION_LOW_MEMORY
};
typedef struct {
  XpDefinitionOwnedText text;
  uint16_t start_page, end_page, start_word, end_word;
  uint8_t status;
  uint8_t reserved[3];
} XpDefinitionSelection;
extern "C" {
XpUiEffect xp_ui_definition_update(XpUiDefinitionState*, const XpUiInput*, uint8_t tap, uint8_t long_press,
                                   uint8_t header_long_press, int32_t screen_width, uint8_t touch_controls);
XpUiRect xp_ui_definition_body(const XpUiDefinitionLayout*);
}
