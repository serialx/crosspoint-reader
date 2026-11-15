#pragma once
#include <HalStorage.h>

#include <cstring>
using nvs_handle_t = uint32_t;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, NVS_READWRITE = 1;
inline int nvs_open(const char*, int, nvs_handle_t* handle) {
  *handle = 17;
  return protection_test::step("nvs-open") ? 0 : 2;
}
inline int nvs_get_blob(nvs_handle_t, const char*, void* bytes, size_t* length) {
  if (!protection_test::step("nvs-read")) return 2;
  assert(*length == 32);
  memset(bytes, 0x42, 32);
  return 0;
}
inline int nvs_set_blob(nvs_handle_t, const char*, const void*, size_t) {
  return protection_test::step("nvs-write") ? 0 : 2;
}
inline int nvs_commit(nvs_handle_t) { return protection_test::step("nvs-commit") ? 0 : 2; }
inline void nvs_close(nvs_handle_t) { protection_test::trace.push_back("nvs-close"); }
