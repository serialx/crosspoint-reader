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

// Web policy owns phases, navigation, connection health and pumping cadence.
// run/cancel execute outside ModelLock; publish copies only the 20 B scalar
// state under ModelLock. All descriptor/input pointers are non-null, aligned,
// live and disjoint. Callbacks must not reenter run; cancel borrows its own byte.
typedef struct {
  uint32_t last_handle, first_disconnect;
  int32_t disconnects, bars;
  uint8_t phase, mode, is_ap, start_in_join;
} XpUiWebState;
typedef enum {
  XP_UI_WEB_MODES = 0,
  XP_UI_WEB_WIFI = 1,
  XP_UI_WEB_AP_STARTING = 2,
  XP_UI_WEB_RUNNING = 3,
  XP_UI_WEB_STOPPING = 4
} XpUiWebPhase;
typedef enum {
  XP_UI_WEB_NOW = 0,              // value ignored; returns millis().
  XP_UI_WEB_PAINT = 1,            // value ignored; schedules repaint; return ignored.
  XP_UI_WEB_ENTER_RESOURCES = 2,  // value ignored; logs heap and releases SD font caches; return ignored.
  XP_UI_WEB_EXIT_LOG_START = 3,   // value ignored; logs initial heap; return ignored.
  XP_UI_WEB_EXIT_LOG_END = 4,     // value ignored; logs final heap; return ignored.
  XP_UI_WEB_DNS_STOP = 5,         // value ignored; stops/deletes captive DNS; return ignored.
  XP_UI_WEB_MDNS_END = 6,         // value ignored; stops mDNS; return ignored.
  XP_UI_WEB_PLUGINS_REFRESH = 7,  // value ignored; reloads subscriptions; return ignored.
  XP_UI_WEB_WIFI_PRESENT = 8,     // value ignored; returns WiFi mode is not NULL.
  XP_UI_WEB_WIFI_DISCONNECT = 9,  // value ignored; disconnects STA without erasing credentials; return ignored.
  XP_UI_WEB_AP_DISCONNECT = 10,   // value ignored; stops AP; return ignored.
  XP_UI_WEB_DELAY = 11,           // value is milliseconds; return ignored.
  XP_UI_WEB_RESTART = 12,         // value ignored; silent restart may not return; value ignored.
  XP_UI_WEB_RESTART_JOIN =
      13,                   // value ignored; requests heap-defrag restart into Join and may not return; return ignored.
  XP_UI_WEB_LOG_MODE = 14,  // value is selected mode; logs its label; return ignored.
  XP_UI_WEB_HOME = 15,      // value ignored; navigates home; return ignored.
  XP_UI_WEB_SELECT_MODES = 16,    // value 1 logs initial launch, 0 is child return; starts mode child; return ignored.
  XP_UI_WEB_SELECT_CALIBRE = 17,  // value ignored; starts Calibre child; return ignored.
  XP_UI_WEB_WIFI_STA = 18,        // value ignored; logs and sets STA mode; return ignored.
  XP_UI_WEB_SELECT_WIFI = 19,     // value ignored; logs and starts WiFi child; return ignored.
  XP_UI_WEB_AP_START_LOG = 20,    // value ignored; logs AP setup and heap; return ignored.
  XP_UI_WEB_WIFI_AP = 21,         // value ignored; sets AP mode; return ignored.
  XP_UI_WEB_AP_BEGIN = 22,        // value ignored; starts configured AP; returns success.
  XP_UI_WEB_AP_DETAILS = 23,      // value ignored; copies AP IP/SSID and logs them; return ignored.
  XP_UI_WEB_MDNS_RESTART = 24,    // value ignored; restarts/logs mDNS; return ignored.
  XP_UI_WEB_DNS_START = 25,       // value ignored; replaces/configures captive DNS; return ignored.
  XP_UI_WEB_AP_READY_LOG = 26,    // value ignored; logs post-AP heap; return ignored.
  XP_UI_WEB_SERVER_BEGIN = 27,    // value ignored; releases caches, allocates/binds/starts server; return ignored.
  XP_UI_WEB_SERVER_RUNNING = 28,  // value ignored; returns server exists and is running.
  XP_UI_WEB_SERVER_DELETE = 29,   // value ignored; deletes server; return ignored.
  XP_UI_WEB_SERVER_READY_LOG = 30,  // value ignored; logs success; return ignored.
  XP_UI_WEB_SERVER_ERROR_LOG = 31,  // value ignored; logs failure; return ignored.
  XP_UI_WEB_WIFI_RSSI = 32,         // value ignored; returns RSSI bits as signed int32.
  XP_UI_WEB_PLUGIN_DRAIN = 33,      // value ignored; drains bounded plugin events; return ignored.
  XP_UI_WEB_DNS_NEXT = 34,          // value ignored; processes DNS if present; return ignored.
  XP_UI_WEB_WIFI_STATUS = 35,    // value ignored; returns connected boolean and retains native status for diagnostics.
  XP_UI_WEB_INPUT_UPDATE = 36,   // value ignored; pumps mapped input; return ignored.
  XP_UI_WEB_EXIT_INPUT = 37,     // value ignored; returns Back-release or Home edge.
  XP_UI_WEB_WATCHDOG = 38,       // value ignored; resets subscribed watchdog; return ignored.
  XP_UI_WEB_YIELD = 39,          // value ignored; yields task; return ignored.
  XP_UI_WEB_HANDLE_CLIENT = 40,  // value ignored; handles one client iteration; return ignored.
  XP_UI_WEB_READ_CANCEL = 41,    // value ignored; returns independent upload cancellation latch.
  XP_UI_WEB_LOG_GAP = 42,        // value is client gap milliseconds; return ignored.
  XP_UI_WEB_LOG_WIFI_LOST =
      43,  // value is lost-link milliseconds; logs retained status and published count; return ignored.
  XP_UI_WEB_LOG_ABANDON = 44,      // value ignored; logs five-minute abandonment; return ignored.
  XP_UI_WEB_LOG_RECOVERED = 45,    // value is recovery milliseconds; logs published failed-check count; return ignored.
  XP_UI_WEB_LOG_WEAK = 46,         // value is RSSI bits as signed int32; return ignored.
  XP_UI_WEB_USB_ACTIVITY = 47,     // value ignored; opens USB activity when supported; return ignored.
  XP_UI_WEB_LOG_WIFI_RESULT = 48,  // value is connected boolean; return ignored.
  XP_UI_WEB_USB_SUPPORTED = 49,    // value ignored; returns compile-time USB MSC capability.
  XP_UI_WEB_AP_ERROR_LOG = 50,     // value ignored; logs AP start failure; return ignored.
  XP_UI_WEB_CLEAR_CONNECTION = 51  // value ignored; clears owned IP/SSID on enter; return ignored.
} XpUiWebCall;
typedef struct {
  void* context;
  uint32_t (*call)(void*, uint32_t operation, uint32_t value);
  void (*publish)(void*, const XpUiWebState*);
} XpUiWebServices;
// Enter/Exit/Tick retain shared event numbers 0/1/2. Input(3) carries a mode
// (0..3 or 255 for cancel); ChildResult(4) carries WiFi success; event 5 returns
// from the Calibre child. The last-WiFi-check timestamp retains static lifetime.
void xp_ui_network_web_run(XpUiWebState*, const XpUiWebServices*, uint8_t event, uint8_t value);
// This separate byte belongs to Rust cancellation policy. The transport may
// update it during handleClient without aliasing the run's borrowed model.
uint8_t xp_ui_network_web_cancel(uint8_t* latch, const XpUiInput*, uint8_t home);
typedef struct {
  XpUiRect screen;
  int32_t font, small_font;
  int16_t top_padding, header_height, tab_height, spacing, side_padding, line_height;
  uint8_t wifi_connected, reserved[3];
} XpUiWebLayout;
enum {
  XP_UI_WEB_SUBHEADER = 1300,
  XP_UI_WEB_STARTING_TEXT = 1301,
  XP_UI_WEB_CONNECT_HINT = 1302,
  XP_UI_WEB_WIFI_QR = 1303,
  XP_UI_WEB_SSID_TEXT = 1304,
  XP_UI_WEB_OPEN_HINT = 1305,
  XP_UI_WEB_SCAN_HINT = 1306,
  XP_UI_WEB_URL_QR = 1307,
  XP_UI_WEB_PRIMARY_URL = 1308,
  XP_UI_WEB_ALTERNATE_URL = 1309,
  XP_UI_WEB_THICK_LINE = 1310  // Native line primitive; Rust provides endpoints and width.
};
// Raw text commands carry alignment; the facade binds native centered-text
// measurement/drawing and QR painting, keeping formatted-string lifetimes.
uint8_t xp_ui_network_web_view(const XpUiWebState*, const XpUiWebLayout*, const XpUiBuffer*, XpUiScene*);

#ifdef __cplusplus
}
#endif
