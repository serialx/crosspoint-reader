#pragma once
#include <nvs.h>
inline void esp_fill_random(void* p, size_t n) {
  printf("random %zu\n", n);
  memset(p, 0x73, n);
}
