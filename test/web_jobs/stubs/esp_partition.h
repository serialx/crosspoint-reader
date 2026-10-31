#pragma once
#include "Oracle.h"
using esp_err_t = int;
constexpr int ESP_OK = 0;
struct esp_partition_t {
  size_t size = 131072;
  const char* label = "ota_1";
  unsigned address = 0x200000;
};
inline esp_partition_t oraclePartition;
inline int esp_partition_read(const esp_partition_t*, size_t, void* out, size_t) {
  *static_cast<uint16_t*>(out) = 5;
  return 0;
}
inline int esp_partition_erase_range(const esp_partition_t*, size_t pos, size_t n) {
  oracle::record("erase %zu %zu", pos, n);
  return oracle::fault == "erase" ? -1 : 0;
}
inline int esp_partition_write(const esp_partition_t*, size_t pos, const void* p, size_t n) {
  oracle::record("partition-write %zu %zu %u", pos, n, oracle::hash(static_cast<const uint8_t*>(p), n));
  return oracle::fault == "partition-write" ? -1 : 0;
}
