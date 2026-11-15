#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
using nvs_handle_t = uint32_t;
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, NVS_READWRITE = 1;
inline int openError = 0, readError = 0, writeError = 0, commitError = 0;
inline size_t secretSize = 32;
inline int nvs_open(const char* name, int mode, nvs_handle_t* h) {
  printf("open %s %d\n", name, mode);
  *h = 17;
  return openError;
}
inline int nvs_get_blob(nvs_handle_t h, const char* key, void* p, size_t* n) {
  printf("get %u %s %zu\n", h, key, *n);
  memset(p, 0x42, *n);
  *n = secretSize;
  return readError;
}
inline int nvs_set_blob(nvs_handle_t h, const char* key, const void* p, size_t n) {
  printf("set %u %s %zu %u\n", h, key, n, *(const uint8_t*)p);
  return writeError;
}
inline int nvs_commit(nvs_handle_t h) {
  printf("commit %u\n", h);
  return commitError;
}
inline void nvs_close(nvs_handle_t h) { printf("close %u\n", h); }
