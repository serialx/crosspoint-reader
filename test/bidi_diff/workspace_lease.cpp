#include <xp_ffi.h>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>

#include "BidiUtils.h"

namespace {
bool failAllocation = false;
unsigned allocations = 0;
}  // namespace

void* operator new(size_t bytes, const std::nothrow_t&) noexcept {
  ++allocations;
  void* storage = failAllocation ? nullptr : std::malloc(bytes);
  if (storage) std::memset(storage, 0xa5, bytes);
  return storage;
}

void lease() {
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

void nativeWrappers() {
  const std::string first = "\xd7\x90\xd7\x91";
  const std::string second = "\xd7\x92\xd7\x93";
  const std::vector<std::string> words{first, second};
  const std::string packed = first + '\0' + second + '\0';
  const uint16_t offsets[]{0, 5};
  uint16_t order[]{99, 99};
  std::vector<uint16_t> vectorOrder{99};
  std::string visual = "unchanged";
  std::string shaped = "prefix";
  failAllocation = true;
  assert(!BidiUtils::applyBidiVisual(first.c_str(), visual));
  assert(visual == "unchanged");
  BidiUtils::appendShapedRtlTokens(first.c_str(), shaped);
  assert(shaped == "prefix");
  assert(!BidiUtils::computeVisualWordOrder(words, true, vectorOrder));
  assert(vectorOrder.empty());
  assert(!BidiUtils::computeVisualWordOrder(std::span<const char>(packed), offsets, true, order));
  assert(order[0] == 99 && order[1] == 99 && allocations == 4);
  failAllocation = false;
  for (int repeat = 0; repeat < 2; ++repeat) {
    assert(BidiUtils::applyBidiVisual(first.c_str(), visual));
    assert(visual == "\xd7\x91\xd7\x90");
    shaped = "prefix";
    BidiUtils::appendShapedRtlTokens(first.c_str(), shaped);
    assert(shaped == "prefix\xd7\x91\xd7\x90");
    assert(BidiUtils::computeVisualWordOrder(words, true, vectorOrder));
    assert((vectorOrder == std::vector<uint16_t>{1, 0}));
    assert(BidiUtils::computeVisualWordOrder(std::span<const char>(packed), offsets, true, order));
    assert(order[0] == 1 && order[1] == 0 && allocations == 5);
  }
}

int main(int argc, char**) {
  if (argc > 1) {
    nativeWrappers();
  } else {
    lease();
  }
}
