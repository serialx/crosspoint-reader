#pragma once
#include <cstddef>
#include <cstdint>
constexpr uint32_t MALLOC_CAP_8BIT = 1 << 2;
extern "C" size_t heap_caps_get_largest_free_block(uint32_t);
