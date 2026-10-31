#include <SdCardFontRegistry.h>

#include <cstdio>
#include <cstdlib>
#include <new>

namespace {
struct Block {
  void* pointer;
  size_t bytes;
  size_t id;
  bool filename;
};
Block blocks[1024];
size_t requests, live, peak, failAt, budget, names;
bool tracking;

void* allocate(size_t bytes, bool filename) {
  if (!tracking) return std::malloc(bytes ? bytes : 1);
  ++requests;
  if (requests == failAt || (budget && live + bytes > budget)) {
    std::printf("deny %zu %zu %s live=%zu\n", requests, bytes, filename ? "name" : "container", live);
    if (filename) return nullptr;
    // The firmware's exception-disabled vector growth terminates on OOM.
    // Exit the subprocess here; never unwind through the Rust C ABI.
    std::fflush(stdout);
    std::_Exit(77);
  }
  void* p = std::malloc(bytes ? bytes : 1);
  if (!p) std::abort();
  for (auto& block : blocks) {
    if (block.pointer) continue;
    block = {p, bytes, requests, filename};
    live += bytes;
    peak = live > peak ? live : peak;
    if (filename) ++names;
    std::printf("alloc %zu %zu %s live=%zu names=%zu\n", requests, bytes, filename ? "name" : "container", live, names);
    return p;
  }
  std::abort();
}
void release(void* p) {
  if (!p) return;
  for (auto& block : blocks) {
    if (block.pointer != p) continue;
    live -= block.bytes;
    if (block.filename) --names;
    std::printf("free %zu %zu live=%zu names=%zu\n", block.id, block.bytes, live, names);
    block = {};
    break;
  }
  std::free(p);
}
}  // namespace
void* operator new(size_t n) { return allocate(n, false); }
void* operator new[](size_t n) { return allocate(n, true); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return allocate(n, true); }
void operator delete(void* p) noexcept { release(p); }
void operator delete(void* p, size_t) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete[](void* p, size_t) noexcept { release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { release(p); }
int main(int argc, char** argv) {
  if (argc > 1) failAt = std::strtoul(argv[1], nullptr, 10);
  if (argc > 2) budget = std::strtoul(argv[2], nullptr, 10);
  tracking = true;
  {
    SdCardFontRegistry registry;
    const bool found = registry.discover();
    std::printf("result %d families=%d\n", found, registry.getFamilyCount());
    for (const auto& family : registry.getFamilies()) {
      std::printf("family %s vector=%d files=%zu\n", family.name.c_str(), family.vector, family.files.size());
      for (const auto& file : family.files) {
        std::printf("file %s size=%u style=%u\n", file.name ? file.name.get() : "canonical", file.pointSize,
                    file.style);
      }
    }
  }
  std::printf("summary requests=%zu peak=%zu live=%zu\n", requests, peak, live);
  tracking = false;
  return live ? 1 : 0;
}
