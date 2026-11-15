#include <HalStorage.h>
#include <SdCardFont.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <new>

namespace {
struct alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__) Allocation {
  size_t size;
};
size_t live = 0, peak = 0, largest = 0, requests = 0;
}  // namespace
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  auto* allocation = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
  if (!allocation) return nullptr;
  allocation->size = size;
  live += size;
  peak = std::max(peak, live);
  largest = std::max(largest, size);
  ++requests;
  return allocation + 1;
}
void* operator new[](size_t size) {
  void* allocation = ::operator new[](size, std::nothrow);
  if (!allocation) std::abort();
  return allocation;
}
void operator delete[](void* pointer) noexcept {
  if (!pointer) return;
  auto* allocation = static_cast<Allocation*>(pointer) - 1;
  live -= allocation->size;
  std::free(allocation);
}
void operator delete[](void* pointer, size_t) noexcept { ::operator delete[](pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { ::operator delete[](pointer); }
int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    std::ifstream file(argv[i], std::ios::binary);
    if (!file) return 2;
    sdFontTestFile.assign(std::istreambuf_iterator<char>(file), {});
    const size_t initial = live;
    peak = live;
    largest = requests = 0;
    {
      SdCardFont font;
      const bool ok = font.load("font.cpfont");
      std::printf("%d requests=%zu peak=%zu largest=%zu resident=%zu\n", ok, requests, peak - initial, largest,
                  live - initial);
    }
    if (live != initial) return 3;
  }
}
