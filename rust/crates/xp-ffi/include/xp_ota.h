#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// All calls are synchronous. Buffers and contexts are borrowed for the call.
// State/services must remain valid and exclusive until return. Leaves must not
// unwind or re-enter; progress may read (but not mutate) the published state.
// TAG/URL return borrowed NUL-terminated parser strings as uintptr-sized values.
// Diagnostic callbacks may be null (silent); operation/fetch callbacks are required.
typedef struct {
  size_t size, processed, total;
  bool available;
} XpOtaState;
typedef bool (*XpOtaSink)(void*, const uint8_t*, size_t);
typedef enum {
  XP_OTA_PARSER,
  XP_OTA_FEED,
  XP_OTA_HAS_TAG,
  XP_OTA_HAS_FIRMWARE,
  XP_OTA_TAG,
  XP_OTA_URL,
  XP_OTA_SIZE,
  XP_OTA_ASSET,
  XP_OTA_STORE,
  XP_OTA_PARTITION,
  XP_OTA_BEGIN,
  XP_OTA_WRITE,
  XP_OTA_ABORT,
  XP_OTA_END,
  XP_OTA_SELECT,
  XP_OTA_CHIP,
  XP_OTA_PROGRESS
} XpOtaOperation;
typedef struct {
  void* ctx;
  size_t (*operation)(void*, XpOtaOperation, const uint8_t*, size_t);
  bool (*fetch)(void*, bool release, XpOtaSink, void*);
  void (*diagnostic)(void*, uint32_t, size_t, size_t, const char*);
  const uint8_t* board_tag;
  size_t board_tag_len;
  bool has_progress;
} XpOtaServices;
bool xp_ota_newer(bool available, const char* current, const char* latest);
uint32_t xp_ota_check(XpOtaState*, const XpOtaServices*);
uint32_t xp_ota_install(XpOtaState*, const XpOtaServices*, bool newer);

typedef enum { XP_BOOT_FIND, XP_BOOT_SIZE, XP_BOOT_READ, XP_BOOT_ERASE, XP_BOOT_WRITE } XpBootOperation;
typedef struct {
  void* ctx;
  size_t (*operation)(void*, XpBootOperation, size_t, uint8_t*, size_t);
  void (*diagnostic)(void*, uint32_t, uint32_t, uint32_t, uint32_t);
} XpBootServices;
uint32_t xp_ota_seq_crc(uint32_t seq);
bool xp_ota_boot_switch(const XpBootServices*, uint8_t subtype);

typedef enum {
  XP_USB_STOP_SERIAL,
  XP_USB_RESET_PHY,
  XP_USB_PREPARE_INTERRUPTS,
  XP_USB_CREATE_SEMAPHORE,
  XP_USB_ALLOC_INTERRUPT,
  XP_USB_DELETE_SEMAPHORE,
  XP_USB_CONNECT,
  XP_USB_WAIT_RESET,
  XP_USB_DISABLE_INTERRUPTS,
  XP_USB_FREE_INTERRUPT,
  XP_USB_DELAY
} XpUsbOperation;
void xp_ota_usb_handoff(void* ctx, bool (*operation)(void*, XpUsbOperation));
#ifdef __cplusplus
}
#endif
