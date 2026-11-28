#pragma once
#include <cstddef>
constexpr int MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_INTERNAL = 2, MALLOC_CAP_8BIT = 4;
inline size_t heap_caps_get_largest_free_block(int) { return 200 * 1024; }
inline size_t heap_caps_get_free_size(int) { return 200 * 1024; }
