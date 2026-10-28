#pragma once
#include <cstddef>
namespace fake {inline size_t heap=100000,block=50000;}
namespace HalMemory {struct Heap {size_t freeBytes,largestBlockBytes;}; inline Heap getDefaultHeap(){return {fake::heap,fake::block};}}
struct Esp {unsigned getFreeHeap(){return fake::heap;}}; inline Esp ESP;
