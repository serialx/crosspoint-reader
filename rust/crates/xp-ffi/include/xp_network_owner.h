#pragma once
#include "xp_app.h"
#include "xp_ui_network.h"
#ifdef __cplusplus
extern "C" {
#endif

// Immutable physical tables; loans end before unlocking or mutating their storage.
// Native STL storage preserves existing capacity, allocation and failure behavior.
typedef struct {
  const void* string;
  XpAppText bytes;
} XpWifiText;
typedef struct {
  uintptr_t words[7];
} XpWifiLastSsidStorage;
typedef struct {
  uintptr_t words[13];
} XpWifiCredentialStorage;
typedef struct {
  uintptr_t words[6];
} XpWifiKeyboardStorage;
typedef struct {
  XpWifiText ssid;
  int32_t rssi;
  uint8_t encrypted, saved, hidden;
} XpWifiRow;
typedef struct {
  XpWifiText ssid, password;
} XpWifiCredentialView;
typedef struct {
  XpWifiText strings[5];
  XpWifiRow row;
  XpAppText status;
  uint32_t row_count;
  uint8_t row_valid, attempted;
} XpWifiSnapshot;
// Null pointer skips a destination; empty null-data text clears it.
// Native order: optional attempt append, then selected/IP/error/password.
typedef struct {
  const XpWifiText* attempt;
  const XpWifiText* assign[4];
} XpWifiTextEdits;

typedef enum { XP_WIFI_SELECTED, XP_WIFI_IP, XP_WIFI_ERROR, XP_WIFI_PASSWORD, XP_WIFI_MAC } XpWifiString;
typedef enum {
  XP_WIFI_LOG_EXIT_START,
  XP_WIFI_LOG_SCAN_DELETE,
  XP_WIFI_LOG_SCAN_DELETED,
  XP_WIFI_LOG_EXIT_END,
  XP_WIFI_LOG_AUTO_ATTEMPT,
  XP_WIFI_LOG_SAVED,
  XP_WIFI_LOG_HIDDEN_SAVED,
  XP_WIFI_LOG_CONNECTED,
  XP_WIFI_LOG_AUTO_FAILURE,
  XP_WIFI_LOG_MANUAL,
  XP_WIFI_LOG_SNAPSHOT_OOM,
  XP_WIFI_LOG_SCENE_OOM,
  XP_WIFI_LOG_INVALID_SCENE
} XpWifiDiagnostic;
typedef struct {
  const char* row_text;
  const char* mac;
  uint16_t real_count;
} XpWifiPaintSnapshot;
typedef struct {
  uint8_t (*allow_auto)(void*);
  void (*resource_lock)(void*, uint8_t acquire);
  // End derived resources before Rust releases commands, then destroy Activity.
  void (*release_resources)(void*);
  void (*layout)(void*, XpUiWifiLayout*);
  XpUiSettingsResources (*metrics)(void*);
  const char* (*view_text)(void*, uint32_t resource);
  void (*paint)(void*, const XpUiCommand*, XpUiRect, const XpWifiPaintSnapshot*);
  void (*paint_strings_clear)(void*);
  void (*prompt_label)(void*);
  void (*read_mac)(void*);
  // Last-SSID and credential slots are placement-constructed only when acquired.
  void (*last_ssid_drop)(XpWifiLastSsidStorage*);
  XpWifiText (*last_ssid)(XpWifiLastSsidStorage*);
  uint8_t (*credential_lookup)(XpWifiCredentialStorage*, XpWifiText, XpWifiCredentialView*);
  void (*credential_drop)(XpWifiCredentialStorage*);
  void (*store_load)(void);
  void (*store_save)(XpWifiText ssid, XpWifiText password);
  void (*store_remove)(void*, XpWifiText ssid);
  void (*store_last)(XpWifiText ssid);
  void (*snapshot)(void*, uint32_t row_index, const XpWifiText* attempted, XpWifiSnapshot*);
  void (*text_apply)(void*, const XpWifiTextEdits*);
  void (*rows_reset)(void*, uint32_t reserve, uint8_t clear_statuses);
  // UINT32_MAX appends; non-null scan index preserves SSID/encryption/store/push order.
  void (*row_write)(void*, uint32_t target, const XpWifiRow*, const uint32_t* scan_index);
  void (*rows_sort)(void*, uint8_t (*before)(uint8_t, int32_t, uint8_t, int32_t));
  void (*statuses_rebuild)(void*, uint8_t (*signal_strength)(int32_t));
  uint32_t (*attempts_reset)(void*);
  void (*station_mode)(void);
  void (*disconnect)(void);
  void (*scan_begin)(void);
  int32_t (*scan_complete)(void);
  void (*scan_ssid)(uint32_t, char out[33]);
  int32_t (*scan_rssi)(uint32_t);
  void (*scan_delete)(void);
  // Fixed SDK setup: persistent/mode/disconnect/delay/scan/sort/hostname/begin.
  void (*connect)(XpWifiText ssid, XpWifiText password, uint8_t use_password);
  int32_t (*connection_status)(void);
  void (*connection_ip)(char out[16]);
  void (*connected_diagnostic)(void);
  uint8_t (*clock_available)(void);
  uint8_t (*clock_synced)(void);
  uint8_t (*clock_sync)(void);
  void (*clock_mark_and_save)(void);
  void (*trusted_time_start)(void);
  void (*plugin_drain)(void*);
  void (*diagnostic)(void*, XpWifiDiagnostic, uint32_t bytes);
  // Native result projection retained for the ten existing parent callers.
  void (*prepare_result_retire_zc1)(void*, uint8_t connected);
  XpWifiText (*keyboard_text)(XpWifiKeyboardStorage*, XpAppText);
  void (*keyboard_text_drop)(XpWifiKeyboardStorage*);
} XpWifiPhysical;
extern const XpWifiPhysical XP_WIFI_PHYSICAL;

typedef struct {
  void (*layout)(void*, XpUiSettingsLayout*);
  XpUiSettingsResources (*resources)(void*);
  void (*paint)(void*, const XpUiCommand*, XpUiRect);
  void (*scroll_diagnostic)(int32_t delta, uint16_t count);
  void (*scene_diagnostic)(uint8_t invalid);
  void (*prepare_result_retire_n8)(void*, uint8_t cancelled, uint8_t mode);
} XpNetworkModesPhysical;
extern const XpNetworkModesPhysical XP_NETWORK_MODES_PHYSICAL;
#ifdef __cplusplus
}
#endif
