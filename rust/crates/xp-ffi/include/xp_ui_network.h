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

#ifdef __cplusplus
}
#endif
