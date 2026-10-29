#pragma once
#include <Arduino.h>
constexpr int MALLOC_CAP_8BIT = 1;
inline size_t largestBlock = 65536;
inline size_t heap_caps_get_largest_free_block(int) {
  printf("heap %zu\n", largestBlock);
  return largestBlock;
}
