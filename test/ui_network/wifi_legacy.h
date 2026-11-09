#pragma once
// Test-only native predecessor descriptors and bridges; absent from firmware ABI.
#include <xp_ui_network.h>
#ifdef __cplusplus
extern "C" {
#endif
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
uint8_t xp_test_network_wifi_view(const XpUiWifiState* state, const XpUiWifiLayout* layout,
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
void xp_test_network_wifi_run(XpUiWifiState* state, const XpUiWifiServices* services, uint8_t event, uint32_t value,
                              const XpUiInput* input);
// Counts of visible-row snapshots and scene commands, not bytes. A successful
// budget bounds one view build, allowing one checked allocation before emission.
typedef struct {
  uint16_t rows, commands;
} XpUiWifiBudget;
uint8_t xp_test_network_wifi_budget(const XpUiWifiState* state, const XpUiWifiLayout* layout, int32_t line_height,
                                    XpUiWifiBudget* budget);
// Consume UI invalidation and advance tap-flash bookkeeping once per SDK-style frame.
void xp_test_network_wifi_prepare_view(XpUiWifiState* state);
void xp_test_network_wifi_publish(XpUiWifiState* destination, XpUiWifiState* incoming);
void xp_test_network_wifi_view_publish(XpUiWifiState* destination, const XpUiWifiState* painted,
                                       const XpUiListState* viewport);
// Press maps row value to its published ordinal; release maps to the absolute row index.
uint8_t xp_test_network_wifi_hit(const XpUiWifiHits* hits, const XpUiWifiState* state, XpUiInput* input);
uint8_t xp_test_network_wifi_signal_strength(int32_t rssi);
uint8_t xp_test_network_wifi_row_before(uint8_t saved, int32_t rssi, uint8_t other_saved, int32_t other_rssi);
#ifdef __cplusplus
}
#endif
