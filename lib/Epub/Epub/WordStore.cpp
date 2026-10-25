#include "WordStore.h"

#include <Logging.h>
#include <Memory.h>
#include <MemoryManager.h>

namespace {
constexpr size_t RECLAIM_HEADROOM = 8 * 1024;

void* allocateParagraphBytes(const size_t bytes) {
  auto data = makeUniqueNoThrow<uint8_t[]>(bytes);
  if (!data) {
    freeink::MemoryManager::instance().ensureFree(bytes + RECLAIM_HEADROOM);
    data = makeUniqueNoThrow<uint8_t[]>(bytes);
  }
  if (!data) LOG_ERR("PTX", "OOM: paragraph storage (%zu bytes)", bytes);
  // Rust owns this C-ABI allocation and returns it to releaseParagraphBytes.
  return data.release();
}
void releaseParagraphBytes(void* data) { delete[] static_cast<uint8_t*>(data); }
const XpParaAllocator ALLOCATOR = {allocateParagraphBytes, releaseParagraphBytes};
}  // namespace

const XpParaAllocator* paragraphAllocator() { return &ALLOCATOR; }

WordStore::WordStore() { xp_word_store_init(handle(), &ALLOCATOR); }
WordStore::~WordStore() { xp_word_store_destroy(handle()); }

bool WordStore::append(const char* text, const size_t len, StoredWord& out) {
  XpStoredWord stored;
  if (!xp_word_store_append(handle(), reinterpret_cast<const uint8_t*>(text), len, &stored)) return false;
  out = {stored.chunk, stored.off, stored.len};
  return true;
}

void WordStore::release(const StoredWord& word) { xp_word_store_release(handle(), {word.chunk, word.off, word.len}); }
