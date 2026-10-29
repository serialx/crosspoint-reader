#pragma once
#include "xp_ui.h"
#include "xp_ui_settings.h"
#ifdef __cplusplus
extern "C" {
#endif

// Private network-family ABI. Descriptors are aligned, live and disjoint. The
// facade owns strings/scan rows; callbacks lend them only for the current call.
// Calls execute outside ModelLock. Publish copies the model under that lock.
typedef enum {
  XP_UI_NETWORK_MODES = 0,
  XP_UI_NETWORK_WIFI = 1,
  XP_UI_NETWORK_WEB = 2,
  XP_UI_NETWORK_CALIBRE = 3,
  XP_UI_NETWORK_USB = 4
} XpUiNetworkKind;
typedef enum {
  XP_UI_USB_UNSUPPORTED = 0,
  XP_UI_USB_WAITING = 1,
  XP_UI_USB_CONNECTED = 2,
  XP_UI_USB_EJECTED = 3,
  XP_UI_USB_DISCONNECTED = 4,
  XP_UI_USB_IO_ERROR = 5
} XpUiUsbPhase;
typedef enum {
  XP_UI_NETWORK_ENTER = 0,
  XP_UI_NETWORK_EXIT = 1,
  XP_UI_NETWORK_TICK = 2,
  XP_UI_NETWORK_INPUT = 3,
  XP_UI_NETWORK_CHILD_RESULT = 4
} XpUiNetworkEvent;
typedef enum {
  XP_UI_NETWORK_NOW = 0,
  XP_UI_NETWORK_PAINT = 1,
  XP_UI_NETWORK_PAINT_WAIT = 2,
  XP_UI_NETWORK_DELAY = 3,
  XP_UI_NETWORK_DIAGNOSTIC = 4,
  XP_UI_USB_BEGIN = 5,
  XP_UI_USB_END = 6,
  XP_UI_USB_STATE = 7,
  XP_UI_USB_SUSPENDED = 8,
  XP_UI_USB_DISCONNECT = 9,
  XP_UI_USB_RESTART = 10,
  // PaintWait returns 1 only after the preparing frame finishes displayBuffer.
  // Poll returns 0 pending, 1 displayed, or 2 rejected (consumes the rejection).
  XP_UI_USB_PREPARING_STATUS = 11,
  // Leave before storage handoff; no Storage.endUsbDrive or restart is needed.
  XP_UI_USB_CANCEL_PREPARING = 12
} XpUiNetworkCall;
typedef enum {
  XP_UI_USB_START_FAILURE = 0,
  XP_UI_USB_SUSPEND_TIMEOUT = 1,
  XP_UI_USB_WAIT_TIMEOUT = 2,
  XP_UI_USB_START_TIMEOUT = 3,
  XP_UI_USB_IO_FAILURE = 4,
  XP_UI_USB_DISCONNECT_FAILURE = 5,
  XP_UI_USB_DISCONNECT_TIMEOUT = 6
} XpUiUsbDiagnostic;
typedef struct {
  uint32_t host_wait_at, start_failure_at, forced_disconnect_at, host_suspend_at;
  uint8_t phase, preparing, start_failed, restart_requested;
  uint8_t forced_disconnect_requested, host_suspend_pending, reserved[2];
} XpUiUsbState;
typedef struct {
  void* context;
  uint32_t (*call)(void*, uint32_t operation, uint32_t value);
  void (*publish)(void*, const XpUiUsbState*);
} XpUiUsbServices;
// Home is supplied separately because global gesture ownership stays in C++.
void xp_ui_network_usb_run(XpUiUsbState*, const XpUiUsbServices*, uint8_t event, const XpUiInput*, uint8_t home);
uint8_t xp_ui_network_usb_prevent_sleep(const XpUiUsbState*);

// Every text/bitmap handle stays valid through painting. Header/footer are
// shared chrome resources; all text placement and body geometry live in Rust.
typedef struct {
  XpUiRect screen, safe;
  int32_t small_font;
  int16_t top_padding, header_height, hints_height, side_padding, space_md;
  uint16_t reserved;
} XpUiNetworkLayout;
typedef struct {
  void* context;
  int16_t (*height)(void*, uint32_t handle, int32_t font, int16_t width, uint8_t bold, uint8_t lines);
} XpUiNetworkTextServices;
enum {
  XP_UI_NETWORK_HEADER = 1000,
  XP_UI_NETWORK_FOOTER = 1001,
  XP_UI_USB_PREPARING_TEXT = 1100,
  XP_UI_USB_EJECT_TEXT = 1101,
  XP_UI_USB_WAITING_TEXT = 1102,
  XP_UI_USB_CONNECTED_TEXT = 1103,
  XP_UI_USB_DELAY_TEXT = 1104,
  XP_UI_USB_START_ERROR_TEXT = 1105,
  XP_UI_USB_ERROR_TEXT = 1106
};
uint8_t xp_ui_network_usb_view(const XpUiUsbState*, const XpUiNetworkLayout*, const XpUiNetworkTextServices*,
                               const XpUiBuffer*, XpUiScene*);
// Transfer-mode rows wrap subtitles to two lines and use the SDK automatic row
// padding. Shared settings descriptors and list input/hit policy are reused.
// Return codes match xp_ui_settings_view: 1 success, 2 capacity, 0 invalid.
uint8_t xp_ui_network_modes_view(const XpUiListState*, const XpUiSettingsLayout*, const XpUiSettingsResources*,
                                 const XpUiBuffer*, XpUiListState* next, XpUiScene*, XpUiSettingsHits*);

// Calibre transport leaves run outside ModelLock. Upload strings stay owned by
// C++ until DropUpload; publish's flags copy/clear names under the resource mutex.
typedef enum {
  XP_UI_CALIBRE_WIFI = 0,
  XP_UI_CALIBRE_STARTING = 1,
  XP_UI_CALIBRE_RUNNING = 2,
  XP_UI_CALIBRE_ERROR = 3
} XpUiCalibrePhase;
typedef enum {
  XP_UI_CALIBRE_NOW = 0,                 // value ignored; returns millis().
  XP_UI_CALIBRE_PAINT = 1,               // value ignored; schedules repaint; return ignored.
  XP_UI_CALIBRE_WIFI_CONNECTED = 2,      // value ignored; returns WiFi connected boolean.
  XP_UI_CALIBRE_WIFI_PRESENT = 3,        // value ignored; returns WiFi mode is not NULL.
  XP_UI_CALIBRE_WIFI_DISCONNECT = 4,     // value ignored; disconnects without erasing credentials; return ignored.
  XP_UI_CALIBRE_DELAY = 5,               // value is milliseconds to delay; return ignored.
  XP_UI_CALIBRE_RESTART = 6,             // value ignored; silent restart; return ignored.
  XP_UI_CALIBRE_MDNS_END = 7,            // value ignored; stops mDNS; return ignored.
  XP_UI_CALIBRE_MDNS_BEGIN = 8,          // value ignored; starts/logs mDNS; return ignored.
  XP_UI_CALIBRE_CACHE_RELEASE = 9,       // value ignored; releases SD font caches; return ignored.
  XP_UI_CALIBRE_SERVER_BEGIN = 10,       // value ignored; allocates and starts the server; return ignored.
  XP_UI_CALIBRE_SERVER_RUNNING = 11,     // value ignored; returns server exists and is running.
  XP_UI_CALIBRE_HANDLE_CLIENT = 12,      // value ignored; handles one server iteration; return ignored.
  XP_UI_CALIBRE_WATCHDOG = 13,           // value ignored; resets subscribed task watchdog; return ignored.
  XP_UI_CALIBRE_YIELD = 14,              // value ignored; yields the current task; return ignored.
  XP_UI_CALIBRE_BACK = 15,               // value ignored; returns current Back pressed edge.
  XP_UI_CALIBRE_DROP_UPLOAD = 16,        // value ignored; destroys the borrowed upload status; return ignored.
  XP_UI_CALIBRE_FINISH = 17,             // value ignored; finishes activity; return ignored.
  XP_UI_CALIBRE_CONNECTED_DETAILS = 18,  // value ignored; copies current WiFi IP/SSID; return ignored.
  XP_UI_CALIBRE_SELECT_WIFI = 19,        // value ignored; starts WiFi child activity; return ignored.
  XP_UI_CALIBRE_LOG_GAP = 20             // value is milliseconds since last client pump; return ignored.
} XpUiCalibreCall;
enum {
  XP_UI_CALIBRE_CLEAR_ALL = 1,
  XP_UI_CALIBRE_COPY_UPLOAD = 2,
  XP_UI_CALIBRE_CLEAR_UPLOAD = 4,
  XP_UI_CALIBRE_COPY_COMPLETED = 8,
  XP_UI_CALIBRE_CLEAR_COMPLETED = 16
};
typedef struct {
  uint32_t last_handle, received, total, completed_at, processed_at;
  uint8_t phase, exit_requested, reserved[2];
} XpUiCalibreState;
typedef struct {
  uint32_t received, total, completed_at;
  uint8_t in_progress, filename_changed, reserved[2];
} XpUiCalibreUpload;
typedef struct {
  void* context;
  uint32_t (*call)(void*, uint32_t operation, uint32_t value);
  // CLEAR_ALL on enter; COPY_UPLOAD on changed active upload; CLEAR_UPLOAD on
  // upload end; COPY_COMPLETED for new completion; CLEAR_COMPLETED on expiry.
  void (*publish)(void*, const XpUiCalibreState*, uint32_t resource_changes);
  void (*upload)(void*, XpUiCalibreUpload*);
} XpUiCalibreServices;
// ChildResult uses value = 1 for connected, 0 for cancellation. Other events
// ignore value. The caller supplies the current Back edge in input.pressed.
void xp_ui_network_calibre_run(XpUiCalibreState*, const XpUiCalibreServices*, uint8_t event, const XpUiInput*,
                               uint8_t value);
typedef struct {
  XpUiRect screen;
  int32_t font10, font12, small_font;
  int16_t top_padding, header_height, tab_height, spacing, side_padding;
  int16_t progress_height, line10, text_height12, banner_width;
  uint16_t reserved;
} XpUiCalibreLayout;
enum {
  XP_UI_CALIBRE_SUBHEADER = 1200,
  XP_UI_CALIBRE_PROGRESS = 1201,
  XP_UI_CALIBRE_STARTING_TEXT = 1202,
  XP_UI_CALIBRE_ERROR_TEXT = 1203,
  XP_UI_CALIBRE_SETUP_TEXT = 1204,
  XP_UI_CALIBRE_INSTRUCTION_1 = 1205,
  XP_UI_CALIBRE_INSTRUCTION_2 = 1206,
  XP_UI_CALIBRE_INSTRUCTION_3 = 1207,
  XP_UI_CALIBRE_INSTRUCTION_4 = 1208,
  XP_UI_CALIBRE_STATUS_TEXT = 1209,
  XP_UI_CALIBRE_RECEIVING_TEXT = 1210,
  XP_UI_CALIBRE_COMPLETED_TEXT = 1211
};
// Resources borrow owned strings under the resource mutex through CPU painting.
// now_ms is sampled before scene construction for completion visibility.
uint8_t xp_ui_network_calibre_view(const XpUiCalibreState*, const XpUiCalibreLayout*, uint32_t now_ms,
                                   const XpUiBuffer*, XpUiScene*);

#ifdef __cplusplus
}
#endif
