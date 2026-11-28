#pragma once
#include "xp_app.h"
#include "xp_ui_network.h"
#ifdef __cplusplus
extern "C" {
#endif

// Borrowed native-store copies. Their loans end before the matching drop.
typedef struct {
  uintptr_t words[7];
} XpWifiLastSsidStorage;
typedef struct {
  uintptr_t words[13];
} XpWifiCredentialStorage;
typedef struct {
  XpAppText ssid, password;
} XpWifiCredentialView;
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
// Drawing kinds select existing SDK/theme operations; Rust supplies all text.
typedef enum {
  XP_WIFI_DRAW_TEXT,
  XP_WIFI_DRAW_CENTERED,
  XP_WIFI_DRAW_FLOW_TEXT,
  XP_WIFI_DRAW_WRAPPED,
  XP_WIFI_DRAW_WRAPPED_BOTTOM,
  XP_WIFI_DRAW_HEADER,
  XP_WIFI_DRAW_SUBHEADER,
  XP_WIFI_DRAW_FOOTER,
  XP_WIFI_DRAW_HELP
} XpWifiDrawKind;
typedef struct {
  // FlowText loans labels[1] and labels[2] as writable 64-byte buffers for at most two lines.
  const char* labels[4];
  uint8_t kind;
} XpWifiPaintData;
typedef struct {
  uint8_t (*allow_auto)(void*);
  void (*resource_lock)(void*, uint8_t acquire);
  void (*release_resources)(void*);
  // language is returned fresh in XpUiWifiLayout's former reserved byte.
  void (*layout)(void*, XpUiWifiLayout*);
  int32_t (*width)(void*, const char*, int32_t font, uint8_t bold);
  int32_t (*height)(void*, const char*, int32_t font, uint8_t bold, uint8_t lines, int16_t width);
  int32_t (*line)(void*, int32_t font);
  void (*paint)(void*, const XpUiCommand*, XpUiRect, const XpWifiPaintData*);
  int32_t (*read_mac)(uint8_t out[6]);
  void (*last_ssid_drop)(XpWifiLastSsidStorage*);
  XpAppText (*last_ssid)(XpWifiLastSsidStorage*);
  uint8_t (*credential_lookup)(XpWifiCredentialStorage*, XpAppText ssid, XpWifiCredentialView*);
  void (*credential_drop)(XpWifiCredentialStorage*);
  void (*store_load)(void);
  void (*store_save)(XpAppText ssid, XpAppText password);
  void (*store_remove)(XpAppText ssid);
  void (*store_last)(XpAppText ssid);
  uint32_t (*store_count)(void);
  uint8_t (*store_has)(XpAppText ssid);
  void (*station_mode)(void);
  void (*disconnect)(void);
  void (*scan_begin)(void);
  int32_t (*scan_complete)(void);
  void (*scan_ssid)(uint32_t, char out[33]);
  int32_t (*scan_rssi)(uint32_t);
  uint8_t (*scan_encrypted)(uint32_t);
  void (*scan_delete)(void);
  void (*connect)(const char* ssid, const char* password, uint8_t use_password);
  int32_t (*connection_status)(void);
  void (*connection_ip)(char out[16]);
  void (*connected_diagnostic)(void);
  uint8_t (*clock_available)(void);
  uint8_t (*clock_synced)(void);
  uint8_t (*clock_sync)(void);
  void (*clock_mark_and_save)(void);
  void (*trusted_time_start)(void);
  void (*plugin_drain)(void*);
  void (*diagnostic)(XpWifiDiagnostic, XpAppText text, uint32_t bytes);
  // Native result transport remains until ZC1 retires its ten parent callers.
  void (*prepare_result_retire_zc1)(void*, uint8_t connected, XpAppText ssid, XpAppText ip);
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
