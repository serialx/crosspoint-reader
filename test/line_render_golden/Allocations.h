#pragma once
#include <cstddef>
namespace linealloc {
struct Stats {
  size_t requests, peak, largest, live;
};
void begin();
Stats end();
void* allocate(size_t);
void release(void*);
void* resize(void*, size_t);
int aligned(void**, size_t, size_t);
}  // namespace linealloc
