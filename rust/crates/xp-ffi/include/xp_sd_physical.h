#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Dormant until the CP2 SD owner switch wires xp-core::sdfont::io; no shipping callers.
// Caller-owned, immovable storage; initialize before use and destroy in place.
typedef struct {
  uintptr_t words[sizeof(void*) == 4 ? 3 : 4];
} XpSdFileSlot;
void xp_native_sd_handle_init(XpSdFileSlot* slot);
void xp_native_sd_handle_destroy(XpSdFileSlot* slot);
uint8_t xp_native_sd_file_open(XpSdFileSlot* slot, const char* path, uint32_t* size);
int32_t xp_native_sd_file_read(XpSdFileSlot* slot, uint8_t* bytes, size_t length);
uint8_t xp_native_sd_file_seek(XpSdFileSlot* slot, uint32_t offset);
// file_close finishes I/O but retains the HAL Impl; release destroys then reinitializes the same slot.
void xp_native_sd_file_close(XpSdFileSlot* slot);
#ifdef __cplusplus
}
#endif
