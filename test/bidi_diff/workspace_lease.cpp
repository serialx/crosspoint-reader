#include <xp_ffi.h>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {
bool failAllocation = false;
unsigned allocations = 0;
}

void* operator new(size_t bytes, const std::nothrow_t&) noexcept {
  ++allocations;
  return failAllocation ? nullptr : std::malloc(bytes);
}

int main() {
  const auto size = xp_bidi_workspace_size();
  const auto alignment = xp_bidi_workspace_align();
  failAllocation = true;
  assert(!xp_native_bidi_acquire(size, alignment));
  assert(allocations == 1);
  failAllocation = false;
  // Invalid contracts must release both allocation and mutex, then retry.
  assert(!xp_native_bidi_acquire(size + 1, alignment));
  assert(allocations == 2);
  assert(!xp_native_bidi_acquire(size, 0));
  assert(allocations == 3);
  auto* first = static_cast<unsigned char*>(xp_native_bidi_acquire(size, alignment));
  assert(first && allocations == 4);
  for (size_t i = 0; i < size; ++i) assert(first[i] == 0);
  std::memset(first, 0x5a, size);
  xp_native_bidi_release();
  auto* second = static_cast<unsigned char*>(xp_native_bidi_acquire(size, alignment));
  assert(second == first && allocations == 4);
  for (size_t i = 0; i < size; ++i) assert(second[i] == 0x5a);
  xp_native_bidi_release();
}
