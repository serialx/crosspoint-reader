#include <BuildScratch.h>
#include <ContentProtection.h>
#include <FsHelpers.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <InflateStream.h>
#include <Logging.h>
#include <Memory.h>
#include <TxtToHtml.h>
#include <ZipFile.h>
#include <xp_epub_session.h>

#include <cassert>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "physical_defaults.h"

static std::unordered_set<void*> nativeBuffers;
static size_t allocationAttempt = 0;
static void* itemAllocate(size_t length) {
  const char* fault = std::getenv("BOOK_BUFFER_FAIL");
  if (fault && ++allocationAttempt == std::strtoul(fault, nullptr, 10)) return nullptr;
  void* buffer = std::malloc(length);
  if (buffer) nativeBuffers.insert(buffer);
  return buffer;
}
static void itemRelease(void* buffer) {
  nativeBuffers.erase(buffer);
  std::free(buffer);
}
#define malloc itemAllocate
#define free itemRelease

class Txt {
 public:
  static bool isTxtOrMd(std::string_view);
  static bool streamTxtToHtml(const std::string&, Print&);
};
// ACTUAL_TXT_METHODS
class Epub {
 public:
  std::string filepath;
  std::unique_ptr<freeink::content::ContentDecryptor> decryptor;
  // This oracle constructs the ordinary native book; facade composition is separate.
  struct BorrowedBook {
    void* context;
    const XpEpubBookBorrow& callbacks;
  };
  std::optional<BorrowedBook> borrowedBook;
  bool requireOwned() const {
    assert(!borrowedBook);
    return !borrowedBook;
  }
  struct ItemContext;
  static XpBookIoServices itemServices(ItemContext&);
  uint8_t* readItemContentsToBytes(const std::string&, size_t*, bool) const;
  bool readItemContentsToStream(const std::string&, Print&, size_t, bool) const;
  bool getItemSize(const std::string&, size_t*) const;
};
// ACTUAL_ITEM_METHODS
// ACTUAL_TEXT_HELPERS
struct BookMetadataCache {
  static void lockReadCursor() { std::abort(); }
  static void unlockReadCursor() { std::abort(); }
};
static const XpAppResources resources = [] {
  auto value = unexpectedAppResources();
  // ACTUAL_ALLOCATOR_LEAVES
  return value;
}();
static const XpEpubLeaves leaves = [] {
  auto value = unexpectedEpubLeaves();
  // ACTUAL_LEAVES
  return value;
}();
extern "C" int64_t xp_epub_test_items(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText, uint8_t,
                                      XpBookSink, void*);
extern "C" size_t xp_test_live_allocations();
static std::vector<uint8_t> output;
static bool reject;
static uint8_t receive(void*, const uint8_t* bytes, size_t length) {
  printf("sink %zu %u\n", length, !reject);
  output.insert(output.end(), bytes, bytes + length);
  return !reject;
}
struct Output : Print {
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t length) override { return receive(nullptr, bytes, length) ? length : 0; }
};
int main(int argc, char** argv) {
  assert(argc == 7);
  const bool native = std::string(argv[1]) == "native";
  const std::string path = argv[2], href = argv[3];
  const auto kind = static_cast<uint8_t>(std::stoul(argv[4]));
  BookFault::failAt = std::stoul(argv[5]);
  reject = std::stoul(argv[6]) != 0;
  int64_t result = -1;
  if (native) {
    std::string error;
    Epub epub{path, freeink::content::openProtectedBook(path, error)};
    if (error.empty()) {
      if (kind == 0) {
        size_t size = 0;
        if (epub.getItemSize(href, &size)) result = static_cast<int64_t>(size);
      } else if (kind <= 2) {
        Output stream;
        result = epub.readItemContentsToStream(href, stream, 1024, kind == 2);
      } else {
        size_t size = 12345;
        uint8_t* bytes = epub.readItemContentsToBytes(href, &size, kind == 4);
        if (bytes) {
          if (kind == 4) assert(bytes[size] == 0);
          receive(nullptr, bytes, size);
          result = static_cast<int64_t>(size);
        } else {
          assert(size == 12345);
        }
        free(bytes);
      }
    }
  } else {
    result = xp_epub_test_items(&resources, &leaves, text(path), text(href), kind, receive, nullptr);
  }
  assert(xp_test_live_allocations() == 0);
  assert(nativeBuffers.empty());
  printf("result %lld\n", static_cast<long long>(result));
  for (uint8_t byte : output) printf("%02x", byte);
  puts("");
  printf("steps %zu\n", BookFault::step);
}
