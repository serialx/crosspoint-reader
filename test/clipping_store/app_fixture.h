#pragma once
#include "reader_fixture.h"
#include "reader_storage.h"

extern "C" {
size_t xp_test_live_allocations();
void xp_app_test_clipping_observe(void (*)(const XpAppBook*, uint8_t), void (*)());
void xp_epub_test_clipping_observe(void*, void (*)(uint32_t), void (*)(uint32_t, const XpClippingRecord*));
}

namespace clipping_fixture {
// Configure before App initialization. Records always belong to the real App service.
inline size_t retainedStoreAllocations = 0;
inline size_t readerLiveAllocations() {
  const auto live = xp_test_live_allocations();
  assert(live >= retainedStoreAllocations);
  return live - retainedStoreAllocations;
}
inline bool appStoreRequested = false;
inline bool preserveBookHash = false;
inline std::unique_ptr<StorageAdapter> appStorage;
inline void (*loaded)(const XpAppBook*, uint8_t) = nullptr;
inline void (*unloaded)() = nullptr;
inline void (*ready)(void*) = nullptr;
inline uint8_t (*legacyLoad)(void*) = nullptr;
}  // namespace clipping_fixture
