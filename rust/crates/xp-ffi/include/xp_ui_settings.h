#pragma once
#include "xp_ui.h"
#ifdef __cplusplus
extern "C" {
#endif

// Private settings-family ABI. All non-null descriptors are aligned and live for
// the call; mutable descriptors are exclusive and disjoint. Callbacks cannot
// retain pointers or reenter; row strings remain borrowed through painting.
typedef enum {
  XP_UI_SETTINGS_ROOT = 0,
  XP_UI_SETTINGS_SECTION = 1,
  XP_UI_SETTINGS_ABOUT = 2,
  XP_UI_SETTINGS_BLE = 3,
  XP_UI_SETTINGS_REMAP = 4,
  XP_UI_SETTINGS_CLOCK = 5,
  XP_UI_SETTINGS_SYNC = 6,
  XP_UI_SETTINGS_HOME_BUTTON = 7,
  XP_UI_SETTINGS_LAYOUTS = 8,
  XP_UI_SETTINGS_LANGUAGE = 9,
  XP_UI_SETTINGS_STATUS_BAR = 10,
  XP_UI_SETTINGS_TIMEZONE = 11,
} XpUiSettingsKind;

typedef struct {
  const char *label, *subtitle, *value;
  uint32_t icon;
  uint8_t toggle, checked, enabled, reserved;
} XpUiSettingsRow;
typedef struct {
  void* context;
  uint8_t (*row)(void*, uint16_t, XpUiSettingsRow*);
  int32_t (*width)(void*, const char*, int32_t, uint8_t);
  int32_t (*height)(void*, const char*, int32_t, uint8_t, uint8_t, int16_t);
  int32_t (*line)(void*, int32_t);
  uint16_t count, reserved;
} XpUiSettingsResources;
typedef struct {
  XpUiListStyle list;
  XpUiRect safe;
  int32_t small_font, fixed_font, help_font;
  int16_t top_padding, header_height, hints_height, spacing, tab_height, preview_height, side_padding;
  uint8_t kind, touch, flags, reserved;
} XpUiSettingsLayout;
typedef struct {
  XpUiRect first_row;
  int32_t first_index;
  uint32_t disabled;
  int16_t heights[32], gap;
  uint8_t count, uniform;
} XpUiSettingsHits;
typedef struct {
  uint8_t preserve, automatic;
} XpUiSettingsQuickResume;
typedef struct {
  uint16_t mask;
  uint8_t edited, reserved;
} XpUiSettingsLayouts;

enum {
  XP_UI_SETTINGS_RESET = 4,
  XP_UI_SETTINGS_COMMIT = 5,
  XP_UI_SETTINGS_SAVE_VALUE = 6,
  XP_UI_SETTINGS_POPUP = 7,
  XP_UI_SETTINGS_OPEN = 8
};

// Stable identities for the private family protocol; row order is checked in C++.
enum {
  XP_UI_SETTINGS_ACTION_NONE = 0,
  XP_UI_SETTINGS_ACTION_REMAP_FRONT_BUTTONS = 1,
  XP_UI_SETTINGS_ACTION_CUSTOMISE_STATUS_BAR = 2,
  XP_UI_SETTINGS_ACTION_CLOCK_SETTINGS = 3,
  XP_UI_SETTINGS_ACTION_KOREADER_SYNC = 4,
  XP_UI_SETTINGS_ACTION_OPDS_BROWSER = 5,
  XP_UI_SETTINGS_ACTION_NETWORK = 6,
  XP_UI_SETTINGS_ACTION_BLUETOOTH = 7,
  XP_UI_SETTINGS_ACTION_CLEAR_CACHE = 8,
  XP_UI_SETTINGS_ACTION_CHECK_FOR_UPDATES = 9,
  XP_UI_SETTINGS_ACTION_SD_FIRMWARE_UPDATE = 10,
  XP_UI_SETTINGS_ACTION_LANGUAGE = 11,
  XP_UI_SETTINGS_ACTION_DOWNLOAD_FONTS = 12,
  XP_UI_SETTINGS_ACTION_TEXT_SETTINGS = 13,
  XP_UI_SETTINGS_ACTION_PLUGINS = 14,
  XP_UI_SETTINGS_ACTION_KEYBOARD_LAYOUTS = 15,
  XP_UI_SETTINGS_ACTION_HOME_BUTTON = 16,
  XP_UI_SETTINGS_ACTION_ABOUT = 17,
};
enum {
  XP_UI_SETTINGS_CLOCK_TIMEZONE = 0,
  XP_UI_SETTINGS_CLOCK_DST = 1,
  XP_UI_SETTINGS_CLOCK_FORMAT = 2,
  XP_UI_SETTINGS_CLOCK_SHOW_ON_HOME = 3,
  XP_UI_SETTINGS_CLOCK_SYNC = 4,
};
enum {
  XP_UI_SETTINGS_STATUS_CHAPTER_PAGE_COUNT = 0,
  XP_UI_SETTINGS_STATUS_BOOK_PROGRESS_PERCENTAGE = 1,
  XP_UI_SETTINGS_STATUS_PROGRESS_BAR = 2,
  XP_UI_SETTINGS_STATUS_PROGRESS_BAR_THICKNESS = 3,
  XP_UI_SETTINGS_STATUS_TITLE = 4,
  XP_UI_SETTINGS_STATUS_BATTERY = 5,
  XP_UI_SETTINGS_STATUS_XTC_STATUS_BAR = 6,
  XP_UI_SETTINGS_STATUS_CLOCK = 7,
};
enum {
  XP_UI_SETTINGS_BLE_VIEW_MENU = 0,
  XP_UI_SETTINGS_BLE_VIEW_SCAN = 1,
  XP_UI_SETTINGS_BLE_VIEW_PAIRED = 2,
  XP_UI_SETTINGS_BLE_VIEW_MAPPING = 3,
};
enum {
  XP_UI_SETTINGS_BLE_EVENT_DISCONNECTED = 0,
  XP_UI_SETTINGS_BLE_EVENT_PRESS = 1,
  XP_UI_SETTINGS_BLE_EVENT_RELEASE = 2,
  XP_UI_SETTINGS_BLE_EVENT_REST = 3,
  XP_UI_SETTINGS_BLE_EVENT_TICK = 4,
  XP_UI_SETTINGS_BLE_EVENT_SAVE_FAILED = 5,
  XP_UI_SETTINGS_BLE_EVENT_SAVED = 6,
};
enum {
  XP_UI_SETTINGS_BLE_EFFECT_TOGGLE = 10,
  XP_UI_SETTINGS_BLE_EFFECT_SCAN = 11,
  XP_UI_SETTINGS_BLE_EFFECT_SHOW = 12,
  XP_UI_SETTINGS_BLE_EFFECT_PAIR = 13,
  XP_UI_SETTINGS_BLE_EFFECT_FORGET = 14,
  XP_UI_SETTINGS_BLE_EFFECT_LEARN = 15,
};
enum {
  XP_UI_SETTINGS_SECTION_GENERAL = 0,
  XP_UI_SETTINGS_SECTION_DISPLAY = 1,
  XP_UI_SETTINGS_SECTION_READER = 2,
  XP_UI_SETTINGS_SECTION_CONTROLS = 3,
  XP_UI_SETTINGS_SECTION_LIBRARY = 4,
  XP_UI_SETTINGS_SECTION_NETWORK = 5,
  XP_UI_SETTINGS_SECTION_COUNT = 6,
};
enum { XP_UI_SETTINGS_SHARED_ROWS = 255 };
// Schema kind: toggle=0, enum=1, action=2, range=3, string=4.
XpUiEffect xp_ui_settings_edit(uint8_t kind, uint8_t value, uint8_t count, uint8_t min, uint8_t max, uint8_t step);
XpUiEffect xp_ui_settings_menu_edit(uint8_t kind, int32_t index, uint8_t value);
uint8_t xp_ui_settings_status_value(uint8_t index, uint8_t value);
uint8_t xp_ui_settings_section(uint8_t category, uint8_t field);
uint8_t xp_ui_settings_visible(uint8_t category, uint8_t field, uint8_t text_setting, uint8_t caps, uint8_t footnotes);
uint8_t xp_ui_settings_extra(uint8_t section, uint8_t caps, uint8_t index);
void xp_ui_settings_list_init(XpUiListState*, int32_t selected);
void xp_ui_settings_list_select(XpUiListState*, int32_t selected, uint16_t count);
XpUiInterest xp_ui_settings_list_interest(const XpUiListState*, uint32_t now);
XpUiEffect xp_ui_settings_list_update(XpUiListState*, const XpUiInput*, uint16_t count);
void xp_ui_settings_list_publish(XpUiListState*, const XpUiListState*, int32_t consumed_scroll);
uint8_t xp_ui_settings_hit(const XpUiSettingsHits*, int16_t x, int16_t y, XpUiInteraction*);
// 0 invalid, 1 success, 2 command capacity. Published metadata is unchanged on
// failure; the command buffer is scratch storage and may be partially written.
uint8_t xp_ui_settings_view(const XpUiListState*, const XpUiSettingsLayout*, const XpUiSettingsResources*,
                            const XpUiBuffer*, XpUiListState*, XpUiScene*, XpUiSettingsHits*);
uint8_t xp_ui_settings_quick_resume(XpUiSettingsQuickResume*, uint8_t timeout, uint8_t quick_screen,
                                    uint8_t screen_changed, uint8_t timeout_changed);
typedef struct {
  uint32_t error_until;
  uint8_t mapping[4], step, error, reserved[2];
} XpUiSettingsRemap;
void xp_ui_settings_remap_init(XpUiSettingsRemap*);
XpUiEffect xp_ui_settings_remap_update(XpUiSettingsRemap*, const XpUiInput*, int32_t raw_front);
XpUiEffect xp_ui_settings_sync_update(uint8_t state, const XpUiInput*);
uint8_t xp_ui_settings_sync_result(uint8_t wifi, uint8_t success);
uint8_t xp_ui_settings_layout_locked(const XpUiSettingsLayouts*, uint8_t index);
void xp_ui_settings_layout_toggle(XpUiSettingsLayouts*, uint8_t index);
typedef struct {
  uint32_t shown_signature, last_poll, code, press_ms, since_ms;
  uint8_t view, action, note, reserved;
} XpUiSettingsBle;
typedef struct {
  int16_t code;
  uint8_t label, subtitle, value, enabled, toggle, checked;
} XpUiSettingsBleRow;
uint8_t xp_ui_settings_ble_action(int32_t);
uint8_t xp_ui_settings_ble_status(uint8_t flags, uint8_t failure);
void xp_ui_settings_ble_show(XpUiSettingsBle*, uint8_t view);
uint8_t xp_ui_settings_ble_poll(XpUiSettingsBle*, uint32_t now);
uint8_t xp_ui_settings_ble_back(XpUiSettingsBle*);
uint8_t xp_ui_settings_ble_begin(XpUiSettingsBle*, int32_t index, uint8_t connected, uint32_t now);
uint8_t xp_ui_settings_ble_step(XpUiSettingsBle*, uint8_t kind, uint32_t code, uint32_t at, uint32_t now);
uint8_t xp_ui_settings_ble_clear(XpUiSettingsBle*, int32_t index);
XpUiEffect xp_ui_settings_ble_activate(const XpUiSettingsBle*, int16_t code, uint8_t enabled, uint8_t scanning);
uint8_t xp_ui_settings_ble_row(const XpUiSettingsBle*, uint8_t flags, uint8_t found, uint8_t bonds, uint16_t index,
                               XpUiSettingsBleRow*);
#ifdef __cplusplus
}
#endif
