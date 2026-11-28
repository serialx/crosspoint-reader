#pragma once
#include <xp_ffi.h>
// Host-only compatibility access to the frozen low-level directory corpus.
extern "C" {
// Opaque fields: initialize with xp_zip_begin; do not modify or concurrently use.
typedef struct {
  uint32_t opaque[4];
} XpZipWalker;
// Request/parse functions: 0 success, -1 invalid/truncated input; outputs unchanged on failure.
int32_t xp_zip_eocd_request(uint32_t size, XpZipRead* out);
int32_t xp_zip_eocd(const uint8_t* data, size_t len, XpZipDetails* out);
int32_t xp_zip_begin(uint32_t central, uint32_t start, XpZipWalker* out);
// request: 1 read header, 0 finished, -1 invalid arguments.
int32_t xp_zip_request(const XpZipWalker* walker, XpZipRead* out);
// next: 1 entry (name read request in out), 2 wrapped (request again), 0 end,
// -1 invalid arguments or offset overflow. Supply the reply to the last request;
// short replies end/wrap the scan. A name length >= 256 must be skipped.
int32_t xp_zip_next(XpZipWalker* walker, const uint8_t* data, size_t len, XpZipEntry* out);
int32_t xp_zip_local_request(uint32_t offset, XpZipRead* out);
int32_t xp_zip_data_offset(uint32_t offset, const uint8_t* data, size_t len, uint64_t* out);
}
