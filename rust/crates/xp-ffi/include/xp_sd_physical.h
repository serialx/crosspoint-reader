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
#ifdef XP_SD_FONT_CATALOG
// Directory status: 0 missing, 1 directory, -1 ordinary file. All slots are initialized.
int32_t xp_native_sd_directory_open(XpSdFileSlot*, const char* path);
// Every entry is returned. Name has 128 bytes; name_directories controls name I/O only.
bool xp_native_sd_directory_next(XpSdFileSlot*, char* name, bool* directory, bool name_directories);
bool xp_native_sd_path_exists(const char* path);
// Optional native FreeType metadata; no face, selection or font ownership escapes.
bool xp_native_sd_inspect(const char* path, uint16_t* weight, bool* italic);
typedef enum { XP_SD_INTERNAL, XP_SD_RESIDENT, XP_SD_REQUIRED } XpSdAllocation;
uint8_t* xp_native_sd_allocate(size_t, uint8_t domain);
#else
uint8_t* xp_native_sd_allocate(size_t, bool resident);
#endif
void xp_native_sd_free(uint8_t*, bool resident);
// Events 1..39 are pool diagnostics; 0x100+ load, 0x200+ parse, 0x300 corrupt.
// Prepared catalog builds also use 0x400+ catalog and 0x500+ system events.
void xp_native_sd_diagnostic(const char*, uint32_t event, const uint32_t* args, uint32_t style, uint32_t index,
                             bool seek_failed
#ifdef XP_SD_FONT_CATALOG
                             ,
                             const char* other
#endif
);
#ifdef __cplusplus
}
#endif
