#include <ContentProtection.h>
#include <Memory.h>
#include <xp_epub_session.h>

#include <cassert>
#include <cstring>
#include <mutex>
#include <string>

#include "physical_defaults.h"

extern "C" uint32_t xp_epub_test_book_resources(const XpEpubLeaves*, XpAppText, uint8_t, XpBookSink, void*);
extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_live_allocations();

static bool failOpen, failSeek, failRead, shortRead, failNative, rejectSink, failStream, failRemove;
enum class ProtectionMode { Plain, Encrypted, MissingCredential };
static ProtectionMode protectionMode = ProtectionMode::Plain;
static int opens, closes, born, died, locks, unlocks, depth, sinkCalls;
static const std::string bookPath = "/books/\xff.epub";
static const std::string itemPath =
    "OPS/\xff"
    "chapter.xhtml";
static std::string expected;
void* operator new(size_t size, const std::nothrow_t&) noexcept { return failNative ? nullptr : ::operator new(size); }

struct PhysicalMutex {
  std::mutex mutex;
  void lock() {
    assert(depth == 0 && "cache mutex must not recurse");
    mutex.lock();
    ++depth;
    ++locks;
  }
  void unlock() {
    assert(depth == 1);
    --depth;
    ++unlocks;
    mutex.unlock();
  }
};
static PhysicalMutex sizeWindowLock;
struct BookMetadataCache {
  static void lockReadCursor();
  static void unlockReadCursor();
};
// ACTUAL_MUTEX_METHODS

struct HalFile {
  bool open = false;
  size_t position = 0;
  ~HalFile() {
    assert(depth == 0);
    if (open) ++closes;
  }
  bool seek(uint32_t offset) {
    assert(depth == 1);
    if (failSeek) return false;
    position = offset;
    return true;
  }
  size_t size() const { return 8; }
  int read(uint8_t* bytes, uint32_t length) {
    assert(depth == 1 && position + length <= 8);
    if (failRead) return -1;
    if (shortRead) length /= 2;
    std::memcpy(bytes, &"abcdefgh"[position], length);
    position += length;
    return length;
  }
};
struct PhysicalStorage {
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    assert(std::string(path) == bookPath);
    if (failOpen) return false;
    file.open = true;
    ++opens;
    return true;
  }
  bool removeDir(const char* path) {
    assert(depth == 0 && std::string(path) == bookPath);
    return !failRemove;
  }
} Storage;

struct PhysicalDecryptor final : freeink::content::ContentDecryptor {
  PhysicalDecryptor() { ++born; }
  ~PhysicalDecryptor() override { ++died; }
  bool isEncrypted(const std::string& path) const override {
    assert(depth == 0 && path == itemPath);
    return true;
  }
  size_t decryptedSize(const std::string& path) const override {
    assert(depth == 0 && path == itemPath);
    return 4;
  }
  bool decryptToSink(const std::string& path, freeink::content::ContentChunkSink sink, void* context) override {
    assert(depth == 0 && path == itemPath);
    return !failStream && sink(context, reinterpret_cast<const uint8_t*>("BODY"), 4);
  }
};
std::unique_ptr<freeink::content::ContentDecryptor> freeink::content::openProtectedBook(const std::string& path,
                                                                                        std::string& error) {
  assert(depth == 0 && path == bookPath);
  switch (protectionMode) {
    case ProtectionMode::Plain:
      return {};
    case ProtectionMode::MissingCredential:
      error = "missing credential";
      return {};
    case ProtectionMode::Encrypted:
      return std::make_unique<PhysicalDecryptor>();
  }
  assert(false);
  return {};
}

// ACTUAL_TEXT_HELPERS
static const XpEpubLeaves leaves = [] {
  XpEpubLeaves value = unexpectedEpubLeaves();
  // ACTUAL_LEAVES
  return value;
}();
static uint8_t receive(void*, const uint8_t* bytes, size_t length) {
  assert(depth == 0 && "sink must run after cache guard release");
  assert(std::string(reinterpret_cast<const char*>(bytes), length) == expected);
  ++sinkCalls;
  return !rejectSink;
}
static void reset() {
  assert(depth == 0 && opens == closes && born == died && locks == unlocks);
  assert(xp_test_live_allocations() == 0);
  failOpen = failSeek = failRead = shortRead = failNative = rejectSink = failStream = failRemove = false;
  protectionMode = ProtectionMode::Plain;
  opens = closes = born = died = locks = unlocks = sinkCalls = 0;
  xp_test_fail_allocation_after(SIZE_MAX);
}
static void run(uint8_t kind, uint32_t result) {
  assert(xp_epub_test_book_resources(&leaves, text(bookPath), kind, receive, nullptr) == result);
  assert(depth == 0 && "guard must release on every return");
  assert(opens == closes && born == died && locks == unlocks);
  assert(xp_test_live_allocations() == 0);
}
int main() {
  reset();
  expected = "bcde";
  run(0, 1);
  assert(opens == 1 && locks == 1 && sinkCalls == 1);
  reset();
  failOpen = true;
  run(0, 4);
  assert(opens == 0 && locks == 0);
  reset();
  failNative = true;
  run(0, 4);
  assert(opens == 0 && locks == 0);
  reset();
  failSeek = true;
  run(0, 6);
  assert(opens == 1 && locks == 1 && sinkCalls == 0);
  reset();
  failRead = true;
  run(0, 6);
  assert(opens == 1 && locks == 1 && sinkCalls == 0);
  reset();
  shortRead = true;
  run(0, 6);
  assert(opens == 1 && locks == 1 && sinkCalls == 0);
  reset();
  rejectSink = true;
  run(0, 7);
  assert(opens == 1 && sinkCalls == 1);
  reset();
  run(1, 0);
  assert(born == 0 && sinkCalls == 0);
  reset();
  protectionMode = ProtectionMode::Encrypted;
  expected = "BODY";
  run(1, 1);
  assert(born == 1 && sinkCalls == 1);
  reset();
  protectionMode = ProtectionMode::Encrypted;
  failStream = true;
  run(1, 8);
  assert(born == 1 && sinkCalls == 0);
  reset();
  protectionMode = ProtectionMode::Encrypted;
  rejectSink = true;
  run(1, 8);
  assert(born == 1 && sinkCalls == 1);
  reset();
  protectionMode = ProtectionMode::MissingCredential;
  expected = "missing credential";
  run(1, 2);
  assert(sinkCalls == 1);
  reset();
  protectionMode = ProtectionMode::MissingCredential;
  xp_test_fail_allocation_after(1);
  run(1, 3);
  assert(sinkCalls == 0);
  reset();
  xp_test_fail_allocation_after(0);
  run(0, 3);
  assert(opens == 0);
  reset();
  xp_test_fail_allocation_after(1);
  run(2, 3);
  assert(locks == 1 && unlocks == 1);
  reset();
  run(3, 1);
  reset();
  failRemove = true;
  run(3, 0);
  reset();
}
