#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <new>

namespace {
alignas(std::max_align_t) unsigned char storage[512];
size_t requests = 0;
size_t requested = 0;
size_t throwingCalls = 0;
bool failAllocation = false;
}  // namespace

extern "C" void* nothrowTestMalloc(size_t size) noexcept {
  ++requests;
  requested = size;
  return failAllocation || size > sizeof(storage) ? nullptr : storage;
}

void* operator new(size_t) {
  ++throwingCalls;
  throw std::bad_alloc();
}

void* operator new[](size_t) {
  ++throwingCalls;
  throw std::bad_alloc();
}

int main() {
  using Allocate = void* (*)(size_t, const std::nothrow_t&) noexcept;
  for (Allocate allocate : {static_cast<Allocate>(&::operator new), static_cast<Allocate>(&::operator new[])}) {
    for (const bool fail : {true, false}) {
      for (const size_t size : {size_t{0}, size_t{1}, size_t{128}, std::numeric_limits<size_t>::max()}) {
        requests = requested = throwingCalls = 0;
        failAllocation = fail;
        void* result = allocate(size, std::nothrow);
        const size_t expectedSize = size == 0 ? 1 : size;
        void* expected = fail || size > sizeof(storage) ? nullptr : storage;
        if (result != expected || requests != 1 || requested != expectedSize || throwingCalls != 0) {
          std::fprintf(stderr,
                       "nothrow allocation violated null/request/no-throw contract: size=%zu calls=%zu throws=%zu\n",
                       size, requests, throwingCalls);
          return 1;
        }
      }
    }
  }
  return 0;
}
