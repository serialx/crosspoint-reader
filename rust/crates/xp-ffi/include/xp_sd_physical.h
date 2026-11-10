#pragma once
#include <stddef.h>
#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
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
uint8_t* xp_native_sd_allocate(size_t, bool resident);
void xp_native_sd_free(uint8_t*, bool resident);
// Events 1..39 are pool diagnostics; 0x100+ load, 0x200+ parse, 0x300 corrupt.
void xp_native_sd_diagnostic(const char*, uint32_t event, const uint32_t* args, uint32_t style, uint32_t index,
                             bool seek_failed);
#ifdef __cplusplus
}
#endif
