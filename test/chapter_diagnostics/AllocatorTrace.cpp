// Host-only trace around the production allocator ABI; never linked into firmware.
#define heap_caps_malloc chapter_base_malloc
#define heap_caps_aligned_alloc chapter_base_aligned_alloc
#define heap_caps_realloc chapter_base_realloc
#define heap_caps_free chapter_base_free
#include "../cmake/xp_ffi_host.cpp"
#undef heap_caps_malloc
#undef heap_caps_aligned_alloc
#undef heap_caps_realloc
#undef heap_caps_free
#include <cstdio>
#include <unordered_map>
namespace {
FILE* traceFile() {
  static FILE* file = [] {
    const char* path = std::getenv("CHAPTER_ALLOCATION_TRACE");
    return path ? std::fopen(path, "w") : nullptr;
  }();
  return file;
}
std::unordered_map<uintptr_t, size_t> ids;
size_t nextId = 0;
size_t id(void* ptr) {
  if (!ptr) return 0;
  auto [it, inserted] = ids.emplace(reinterpret_cast<uintptr_t>(ptr), 0);
  if (inserted) it->second = ++nextId;
  return it->second;
}
void record(char operation, size_t size, size_t alignment, size_t before, void* after) {
  if (FILE* file = traceFile()) {
    std::fprintf(file, "%c %zu %zu %zu %zu\n", operation, size, alignment, before, id(after));
    std::fflush(file);
  }
}
}  // namespace
extern "C" void* heap_caps_malloc(size_t size, uint32_t caps) {
  void* result = chapter_base_malloc(size, caps);
  record('A', size, 0, 0, result);
  return result;
}
extern "C" void* heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
  void* result = chapter_base_aligned_alloc(alignment, size, caps);
  record('A', size, alignment, 0, result);
  return result;
}
extern "C" void* heap_caps_realloc(void* ptr, size_t size, uint32_t caps) {
  const auto address = reinterpret_cast<uintptr_t>(ptr);
  const auto before = id(ptr);
  void* result = chapter_base_realloc(ptr, size, caps);
  if (result) ids.erase(address);
  record('R', size, 0, before, result);
  return result;
}
extern "C" void heap_caps_free(void* ptr) {
  const auto before = id(ptr);
  ids.erase(reinterpret_cast<uintptr_t>(ptr));
  record('F', 0, 0, before, nullptr);
  chapter_base_free(ptr);
}
