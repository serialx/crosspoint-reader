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
};
// Raw text commands carry alignment; the facade binds native centered-text
// measurement/drawing and QR painting, keeping formatted-string lifetimes.
uint8_t xp_ui_network_web_view(const XpUiWebState*, const XpUiWebLayout*, const XpUiBuffer*, XpUiScene*);

// Wi-Fi descriptors are borrowed for each call; row and text resources remain C++ owned.
// Credential bytes are never copied into or interpreted by Rust: control sees only flags,
// counts and indexes. View text pointers are opaque and forwarded to native font callbacks.
// LAST_CREDENTIAL retains a native last-SSID copy until run returns. LAST_CREDENTIAL,
// ROW_CREDENTIAL and SELECTED_CREDENTIAL retain a temporary credential until
// DROP_CREDENTIAL (or run return). COPY_AUTO_CREDENTIAL, SELECT_ROW, COPY_SELECTED
// and COPY_PASSWORD copy into native activity strings. Later copies, ENTRY_RESET,
// HIDDEN_RESET and SELECT_ROW clear/replace the applicable strings; destruction frees
// retained capacity. SAVE copies into the credential store; FORGET removes that stored
// copy. Store reload/replacement/destruction also ends stored string lifetimes.
// run executes on the loop task outside ModelLock. view/budget execute on the render
// task with the resource mutex held; list CPU painting uses an owned visible-row copy.
// Non-list CPU painting borrows native text under that mutex. Release it before display.
// prepare_view, publish, view_publish and hit execute under ModelLock: bounded POD only.
// No allocation, free or blocking lock is allowed under ModelLock; never acquire the
// resource mutex while holding ModelLock. The comparator and signal helper are pure.
typedef struct {
  XpUiListState list;  // active is the SDK interaction ordinal; top may include deferred follow feedback.
  uint32_t ui_revision;
  int32_t flash_index;
  uint8_t flash_ticks, ui_invalidated, reserved[2];
  uint32_t connection_at, generation;
  uint16_t count, real_count;
  uint8_t phase, automatic, manual_list, used_saved, requires_password;
  uint8_t save_selection, forget_selection, allow_auto;
} XpUiWifiState;
typedef struct {
  XpUiSettingsLayout base;
  int32_t legacy_body_font;
  uint8_t popup_border, popup_radius, reserved[2];
} XpUiWifiLayout;
typedef struct {
  XpUiSettingsResources rows;
  const char* (*text)(void*, uint32_t);
} XpUiWifiViewServices;
typedef struct {
  XpUiSettingsHits rows;
  XpUiRect buttons[2];
  uint32_t generation;
  uint16_t count;
  uint8_t phase, button_action;
} XpUiWifiHits;
uint8_t xp_ui_network_wifi_view(const XpUiWifiState* state, const XpUiWifiLayout* layout,
                                const XpUiWifiViewServices* services, const XpUiBuffer* buffer, XpUiListState* next,
                                XpUiScene* scene, XpUiWifiHits* hits);
typedef enum {
  XP_UI_WIFI_PHASE_AUTO_CONNECTING = 0,
  XP_UI_WIFI_PHASE_SCANNING = 1,
  XP_UI_WIFI_PHASE_LIST = 2,
  XP_UI_WIFI_PHASE_HIDDEN_SSID = 3,
  XP_UI_WIFI_PHASE_PASSWORD = 4,
  XP_UI_WIFI_PHASE_CONNECTING = 5,
  XP_UI_WIFI_PHASE_CONNECTED = 6,
  XP_UI_WIFI_PHASE_SAVE = 7,
  XP_UI_WIFI_PHASE_FAILED = 8,
  XP_UI_WIFI_PHASE_FORGET = 9
} XpUiWifiPhase;
typedef enum {
  XP_UI_WIFI_CALL_NOW = 0,    // value ignored; returns milliseconds; clock read.
  XP_UI_WIFI_CALL_PAINT = 1,  // value ignored; return ignored; schedules repaint.
  XP_UI_WIFI_CALL_ENTRY_RESET =
      2,  // value ignored; returns credential count; locks render/resource, loads storage, allocates/frees resources.
  XP_UI_WIFI_CALL_READ_MAC =
      3,  // value ignored; return ignored; locks resources, reads device MAC, formats/allocates/logs.
  XP_UI_WIFI_CALL_EXIT = 4,  // value ignored; return ignored; deletes radio scan resources and logs.
  XP_UI_WIFI_CALL_LAST_CREDENTIAL =
      5,  // value ignored; returns credential found; copies last SSID and credential in C++, may allocate/free.
  XP_UI_WIFI_CALL_CREDENTIAL_FLAGS =
      6,  // value ignored; returns bit 0 attempted, bit 1 nonempty password; locks resources.
  XP_UI_WIFI_CALL_COPY_AUTO_CREDENTIAL = 7,  // value ignored; return ignored; locks resources, copies native credential
                                             // into activity/attempt list, allocates/logs.
  XP_UI_WIFI_CALL_DROP_CREDENTIAL =
      8,  // value ignored; return ignored; destroys native temporary credential; may free.
  XP_UI_WIFI_CALL_CLEAR_SCAN =
      9,  // value ignored; return ignored; locks resources, clears native rows/status strings; may free.
  XP_UI_WIFI_CALL_WIFI_STA = 10,    // value ignored; return ignored; sets radio STA mode; may block/allocate/free.
  XP_UI_WIFI_CALL_DISCONNECT = 11,  // value ignored; return ignored; disconnects radio; may block/allocate/free.
  XP_UI_WIFI_CALL_DELAY = 12,       // value is milliseconds; return ignored; blocks loop task.
  XP_UI_WIFI_CALL_SCAN_BEGIN = 13,  // value ignored; return ignored; starts asynchronous radio scan; may allocate/free.
  XP_UI_WIFI_CALL_SCAN_COMPLETE =
      14,  // value ignored; returns signed native scan status/count as uint32 bits; radio read.
  XP_UI_WIFI_CALL_SCAN_RESERVE =
      15,  // value is scan count; return ignored; locks resources, clears/reserves native vector; allocates/frees.
  XP_UI_WIFI_CALL_SCAN_READ = 16,  // value is native scan index; returns nonempty SSID; copies bounded SSID/RSSI in
                                   // C++, radio read, may allocate/free.
  XP_UI_WIFI_CALL_SCAN_FIND =
      17,  // value ignored; returns duplicate row index or UINT32_MAX; locks resources, compares native SSIDs.
  XP_UI_WIFI_CALL_SCAN_APPEND = 18,  // value is native scan index; return ignored; locks resources, appends native
                                     // row/credential flag, radio read, allocates/frees.
  XP_UI_WIFI_CALL_SCAN_RSSI =
      19,  // value is row index or UINT32_MAX for current scan; returns signed RSSI bits; locks resources.
  XP_UI_WIFI_CALL_SCAN_REPLACE =
      20,  // value is duplicate row index; return ignored; locks resources, replaces RSSI/encryption, radio read.
  XP_UI_WIFI_CALL_SCAN_SORT =
      21,  // value ignored; return ignored; locks resources, sorts native rows using Rust comparator.
  XP_UI_WIFI_CALL_ROW_COUNT = 22,  // value ignored; returns native row count; locks resources.
  XP_UI_WIFI_CALL_HIDDEN_APPEND =
      23,  // value ignored; return ignored; locks resources, appends hidden-network placeholder; may allocate/free.
  XP_UI_WIFI_CALL_ROWS_REBUILD =
      24,  // value ignored; return ignored; locks resources, rebuilds native status strings; allocates/frees.
  XP_UI_WIFI_CALL_SCAN_DELETE = 25,  // value ignored; return ignored; releases radio scan resources; may block/free.
  XP_UI_WIFI_CALL_ROW_FLAGS = 26,  // value is row index; returns bits 0 saved, 1 hidden, 2 encrypted; locks resources.
  XP_UI_WIFI_CALL_ROW_ATTEMPTED =
      27,  // value is row index; returns attempted flag; locks resources, compares native strings.
  XP_UI_WIFI_CALL_ROW_CREDENTIAL = 28,  // value is row index; returns credential found; locks resources, copies native
                                        // credential; may allocate/free.
  XP_UI_WIFI_CALL_SELECT_ROW = 29,      // value is row index; return ignored; locks resources, copies native SSID and
                                        // clears password; may allocate/free.
  XP_UI_WIFI_CALL_SELECTED_CREDENTIAL = 30,  // value ignored; returns credential found; locks resources, copies
                                             // selected native credential; may allocate/free.
  XP_UI_WIFI_CALL_COPY_PASSWORD = 31,     // value ignored; return ignored; locks resources, copies temporary credential
                                          // password into activity; may allocate/free.
  XP_UI_WIFI_CALL_PASSWORD_PRESENT = 32,  // value ignored; returns nonempty native password; locks resources.
  XP_UI_WIFI_CALL_LOG_SAVED =
      33,  // value is hidden-network flag; return ignored; locks resources and logs native SSID/password length.
  XP_UI_WIFI_CALL_HIDDEN_RESET =
      34,  // value ignored; return ignored; locks resources, clears native selected SSID/password.
  XP_UI_WIFI_CALL_KEYBOARD =
      35,  // value is hidden-SSID flag; return ignored; allocates/starts keyboard child and callback.
  XP_UI_WIFI_CALL_CONNECTION_RESET =
      36,  // value ignored; return ignored; locks resources, clears native IP/error strings.
  XP_UI_WIFI_CALL_CONNECTION_START = 37,   // value is use-password flag; return ignored; configures/starts radio, reads
                                           // MAC, delays/logs; may allocate/free/block.
  XP_UI_WIFI_CALL_CONNECTION_STATUS = 38,  // value ignored; returns native connection status; radio read.
  XP_UI_WIFI_CALL_CONNECTED_DETAILS = 39,  // value ignored; return ignored; reads radio, copies IP, syncs clocks,
                                           // persists last SSID, drains plugins; locks/allocates/frees/blocks/logs.
  XP_UI_WIFI_CALL_SET_ERROR = 40,     // value 0 general, 1 missing, 2 timeout; return ignored; locks resources, copies
                                      // translated error; may allocate/free.
  XP_UI_WIFI_CALL_LOG_COMPLETE = 41,  // value ignored; return ignored; logs successful completion.
  XP_UI_WIFI_CALL_COMPLETE =
      42,  // value is connected flag; return ignored; copies native result and finishes activity; may allocate/free.
  XP_UI_WIFI_CALL_LOG_AUTO_FAILURE = 43,  // value ignored; return ignored; locks resources and logs native SSID.
  XP_UI_WIFI_CALL_LOG_MANUAL = 44,        // value ignored; return ignored; logs manual-list request.
  XP_UI_WIFI_CALL_SAVE = 45,    // value ignored; return ignored; locks render then resources, copies/persists native
                                // credential; may allocate/free/block.
  XP_UI_WIFI_CALL_FORGET = 46,  // value ignored; return ignored; locks render then resources, removes/persists native
                                // credential and clears row saved flag; may allocate/free/block.
  XP_UI_WIFI_CALL_COPY_SELECTED =
      47,  // value is row index; return ignored; locks resources, copies native SSID; may allocate/free.
  XP_UI_WIFI_CALL_HAPTIC = 48  // value is long-press flag; return ignored; emits hardware touch feedback.
} XpUiWifiCall;
typedef enum {
  XP_UI_WIFI_HEADER = 0x100000,
  XP_UI_WIFI_SUBHEADER = 0x100001,
  XP_UI_WIFI_LIST_FOOTER = 0x100002,
  XP_UI_WIFI_SAVED_FOOTER = 0x100003,
  XP_UI_WIFI_AUTO_FOOTER = 0x100004,
  XP_UI_WIFI_SAVE_FOOTER = 0x100005,
  XP_UI_WIFI_FORGET_FOOTER = 0x100006,
  XP_UI_WIFI_CONNECTED_FOOTER = 0x100007,
  XP_UI_WIFI_FAILED_FOOTER = 0x100008,
  XP_UI_WIFI_LEGEND = 0x100009,
  XP_UI_WIFI_EMPTY = 0x10000a,
  XP_UI_WIFI_RETRY = 0x10000b,
  XP_UI_WIFI_PRESS_SCAN = 0x10000c,
  XP_UI_WIFI_SCANNING = 0x10000d,
  XP_UI_WIFI_FINDING_SAVED = 0x10000e,
  XP_UI_WIFI_CONNECTING = 0x10000f,
  XP_UI_WIFI_CONNECTING_SAVED = 0x100010,
  XP_UI_WIFI_TO_SSID = 0x100011,
  XP_UI_WIFI_NETWORK_SSID = 0x100012,
  XP_UI_WIFI_IP = 0x100013,
  XP_UI_WIFI_CONNECTED = 0x100014,
  XP_UI_WIFI_FAILED = 0x100015,
  XP_UI_WIFI_ERROR = 0x100016,
  XP_UI_WIFI_FORGET = 0x100017,
  XP_UI_WIFI_PROMPT_SSID = 0x100018,
  XP_UI_WIFI_REMOVE = 0x100019,
  XP_UI_WIFI_SAVE_PASSWORD = 0x10001a,
  XP_UI_WIFI_CANCEL = 0x10001b,
  XP_UI_WIFI_YES = 0x10001c,
  XP_UI_WIFI_NO = 0x10001d,
  XP_UI_WIFI_PROMPT_CONNECTED = 0x10001e,
  XP_UI_WIFI_PROMPT_FORGET = 0x10001f
} XpUiWifiResource;
typedef enum {
  XP_UI_WIFI_EVENT_ENTER = 0,
  XP_UI_WIFI_EVENT_EXIT = 1,
  XP_UI_WIFI_EVENT_TICK = 2,
  XP_UI_WIFI_EVENT_KEYBOARD = 3
} XpUiWifiEvent;
typedef struct {
  void* context;
  uint32_t (*call)(void* context, uint32_t operation, uint32_t value);
  // Merge/copy POD under ModelLock; update incoming with merged viewport/publication
  // fields so later publishes retain them (hence mutable). Release ModelLock before
  // taking the resource mutex on List exit to retire rows. An in-flight CPU paint
  // keeps its snapshot until paint ends; otherwise retirement may free it here.
  void (*publish)(void* context, XpUiWifiState* incoming);
} XpUiWifiServices;
// Tick value: bit 0 long press, bit 1 routed touch edge. Keyboard value: bit 0
// cancelled, bit 1 empty result. Enter/Exit ignore value; null input is an empty frame.
void xp_ui_network_wifi_run(XpUiWifiState* state, const XpUiWifiServices* services, uint8_t event, uint32_t value,
                            const XpUiInput* input);
// Counts of visible-row snapshots and scene commands, not bytes. A successful
// budget bounds one view build, allowing one checked allocation before emission.
typedef struct {
  uint16_t rows, commands;
} XpUiWifiBudget;
uint8_t xp_ui_network_wifi_budget(const XpUiWifiState* state, const XpUiWifiLayout* layout, int32_t line_height,
                                  XpUiWifiBudget* budget);
// Consume UI invalidation and advance tap-flash bookkeeping once per SDK-style frame.
void xp_ui_network_wifi_prepare_view(XpUiWifiState* state);
void xp_ui_network_wifi_publish(XpUiWifiState* destination, XpUiWifiState* incoming);
void xp_ui_network_wifi_view_publish(XpUiWifiState* destination, const XpUiWifiState* painted,
                                     const XpUiListState* viewport);
// Press maps row value to its published ordinal; release maps to the absolute row index.
uint8_t xp_ui_network_wifi_hit(const XpUiWifiHits* hits, const XpUiWifiState* state, XpUiInput* input);
uint8_t xp_ui_network_wifi_signal_strength(int32_t rssi);
uint8_t xp_ui_network_wifi_row_before(uint8_t saved, int32_t rssi, uint8_t other_saved, int32_t other_rssi);
#ifdef __cplusplus
}
#endif
