#pragma once
#include <cstddef>
#include <cstdint>
constexpr uint32_t MALLOC_CAP_SPIRAM = 1 << 10, MALLOC_CAP_INTERNAL = 1 << 11, MALLOC_CAP_8BIT = 1 << 2;
extern "C" size_t heap_caps_get_largest_free_block(uint32_t);
extern "C" size_t heap_caps_get_free_size(uint32_t);
struct HeapFixture {
  size_t getMaxAllocHeap() const { return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
  size_t getFreeHeap() const { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
};
inline HeapFixture ESP;
