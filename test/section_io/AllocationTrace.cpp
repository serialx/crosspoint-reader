#include <cstdio>
#include <cstdlib>
#include <new>

namespace {
struct Allocation {
  void* pointer;
  size_t id;
};
Allocation live[65536]{};
size_t count = 0, nextId = 0;
bool hashWindow = false;
size_t hashAllocations = 0;
FILE* trace() {
  static FILE* file = [] {
    const char* path = std::getenv("SECTION_ALLOCATION_TRACE");
    return path ? std::fopen(path, "w") : nullptr;
  }();
  return file;
}
void* allocate(size_t size) {
  if (hashWindow) ++hashAllocations;
  void* pointer = std::malloc(size ? size : 1);
  if (FILE* file = trace(); file && pointer) {
    if (count == 65536) std::abort();
    const size_t id = ++nextId;
    live[count++] = {pointer, id};
    fprintf(file, "allocate %zu %zu\n", id, size);
    fflush(file);
  }
  return pointer;
}
void release(void* pointer) {
  if (FILE* file = trace(); file && pointer) {
    for (size_t i = 0; i < count; ++i) {
      if (live[i].pointer != pointer) continue;
      fprintf(file, "free %zu\n", live[i].id);
      fflush(file);
      live[i] = live[--count];
      break;
    }
  }
  std::free(pointer);
}
}  // namespace
size_t sectionHashAllocationWindow(bool enabled) {
  hashWindow = enabled;
  const auto result = hashAllocations;
  if (enabled) hashAllocations = 0;
  return result;
}
void* operator new(size_t size) {
  void* pointer = allocate(size);
  if (!pointer) std::abort();
  return pointer;
}
void* operator new[](size_t size) { return ::operator new(size); }
void* operator new(size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return allocate(size); }
void operator delete(void* pointer) noexcept { release(pointer); }
void operator delete[](void* pointer) noexcept { release(pointer); }
void operator delete(void* pointer, size_t) noexcept { release(pointer); }
void operator delete[](void* pointer, size_t) noexcept { release(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { release(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { release(pointer); }
