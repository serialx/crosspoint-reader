#include <Memory.h>
#include <xp_epub_session.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "physical_defaults.h"

static std::string output;
static int depth = 0, locks = 0, files = 0;
static unsigned long millis() { return 123; }
struct SerialPort {
  bool connected = true;
  explicit operator bool() const { return connected; }
  template <class... Args>
  void printf(const char* format, Args... arguments) {
    char line[512]{};
    std::snprintf(line, sizeof(line), format, arguments...);
    output += line;
  }
} Serial;
constexpr int O_RDONLY = 1, O_RDWR = 2, O_CREAT = 4, O_TRUNC = 8;
struct FsFile {
  explicit operator bool() const { return false; }
};
struct Volume {
  FsFile open(const char*, int flags) {
    assert(depth == 1);
    assert(flags == O_RDONLY || flags == (O_RDWR | O_CREAT | O_TRUNC));
    return {};
  }
};
class SDCardManager {
  Volume volume;

 public:
  Volume& vol() { return volume; }
  bool openFileForRead(const char*, const char*, FsFile&);
  bool openFileForWrite(const char*, const char*, FsFile&);
} SDCard;
// ACTUAL_SDK_OPEN
struct HalFile {
  struct Impl {
    FsFile file;
    explicit Impl(FsFile&& file) : file(std::move(file)) {}
  };
  std::unique_ptr<Impl> impl;
  HalFile() { ++files; }
  explicit HalFile(std::unique_ptr<Impl> value) : impl(std::move(value)) { ++files; }
  HalFile& operator=(HalFile&&) = default;
  ~HalFile() { --files; }
  bool close() { return false; }
};
class HalStorage {
 public:
  struct StorageLock {
    StorageLock() {
      assert(depth++ == 0);
      ++locks;
    }
    ~StorageLock() { assert(--depth == 0); }
  };
  bool openFileForRead(const char*, const char*, HalFile&);
  bool openFileForWrite(const char*, const char*, HalFile&);
} Storage;
// ACTUAL_HAL_OPEN
// ACTUAL_TEXT_HELPERS
static const XpEpubLeaves leaves = [] {
  auto value = unexpectedEpubLeaves();
  // ACTUAL_OPEN_LEAVES
  value.path_remove = [](XpAppText) -> uint8_t { return 1; };
  value.log_enabled = [](uint8_t) -> uint8_t { return 0; };
  return value;
}();
static const XpAppResources resources = [] {
  auto value = unexpectedAppResources();
  value.path_exists = [](XpAppText) -> uint8_t { return 1; };
  return value;
}();
extern "C" void xp_epub_test_book_open(const XpAppResources*, const XpEpubLeaves*, uint8_t, uint8_t);
extern "C" size_t xp_test_live_allocations();
int main() {
  const char* tags[] = {"ERS", "BMC", "COF", "BMC", "CSS", "ZIP", "TXT", "EBP"};
  for (bool connected : {false, true}) {
    Serial.connected = connected;
    for (uint8_t route = 0; route < 8; ++route) {
      for (uint8_t write = 0; write < 2; ++write) {
        if (write && (route == 1 || route == 5)) continue;
        output.clear();
        const int before = locks;
        xp_epub_test_book_open(&resources, &leaves, route, write);
        assert(files == 0 && depth == 0 && locks == before + 1 && xp_test_live_allocations() == 0);
        std::string path = "/missing/\xff.epub";
        if (route == 2) path += "/.items.bin";
        if (route == 3) path += "/book.bin";
        if (route == 4) path += write ? "/css_rules.cache.tmp" : "/css_rules.cache";
        const std::string expected = "[123] [" + std::string(tags[route]) + "] Failed to open file for " +
                                     (write ? "writing: " : "reading: ") + path + "\n";
        assert(output == (connected ? expected : ""));
      }
    }
  }
}
