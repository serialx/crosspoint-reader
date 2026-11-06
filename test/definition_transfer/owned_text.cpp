#include <cassert>
#include <cstdio>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__linux__)
#include <malloc.h>
#endif
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
static unsigned allocations, frees;
static bool reject;
static void* allocateText(size_t bytes) {
  ++allocations;
  return reject ? nullptr : std::malloc(bytes);
}
static void freeText(void* data) {
  ++frees;
  std::free(data);
}
#define malloc allocateText
#define free freeText
#include "activities/reader/DefinitionQuery.h"
#undef malloc
#undef free
int main() {
  static_assert(!std::is_copy_constructible_v<OwnedSelectionText>);
  static_assert(!std::is_copy_constructible_v<DefinitionQuery>);
  for (size_t len : {0, 1, 15, 16, 4096}) {
    const auto oldAllocations = allocations, oldFrees = frees;
    const std::string source(len, 'x');
    OwnedSelectionText block;
    assert(block.assign(source));
    auto wire = block.borrow();
    assert(wire.len == len && wire.capacity == (len ? len + 1 : 0));
    assert(allocations == oldAllocations + (len != 0));
    if (len) assert(std::memcmp(wire.data, source.c_str(), len + 1) == 0);
    auto* pointer = wire.data;
    size_t usable = wire.capacity;
#if defined(__APPLE__)
    if (pointer) usable = malloc_size(pointer);
#elif defined(__linux__)
    if (pointer) usable = malloc_usable_size(pointer);
#endif
    std::printf("native-host length=%zu requested=%u usable=%zu sso=%zu\n", len, wire.capacity, usable,
                std::string{}.capacity());
    DefinitionQuery pending(std::move(block));
    DefinitionQuery current(std::move(pending));
    DefinitionQuery history(std::move(current));
    assert(std::strcmp(history.c_str(), source.c_str()) == 0);
    assert(pending.empty() && current.empty());
    current = std::move(history);
    auto retained = current.takeOwned();
    retained.clear();
    assert(retained.borrow().data == pointer && retained.borrow().capacity == wire.capacity);
    assert(retained.assign(source));
    assert(retained.borrow().data == pointer);
    reject = true;
    assert(!retained.assign(std::string(len + 20, 'y')));
    reject = false;
    assert(retained.borrow().data == pointer && retained.borrow().len == len);
    assert(std::strcmp(retained.c_str(), source.c_str()) == 0);
    retained = OwnedSelectionText{};
    assert(frees == oldFrees + (len != 0));
  }
  DefinitionQuery initial(std::string("initial"));
  DefinitionQuery previous(std::move(initial));
  assert(std::strcmp(previous.c_str(), "initial") == 0);
}
