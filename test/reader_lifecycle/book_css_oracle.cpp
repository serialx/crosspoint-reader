#include <BuildScratch.h>
#include <Memory.h>
#include <xp_book.h>
#include <xp_epub_session.h>
#include <xp_ffi.h>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static std::vector<std::string> messages;
template <class... Args>
static void record(const char* severity, const char* origin, const char* format, Args... arguments) {
  char message[512]{};
  std::snprintf(message, sizeof(message), format, arguments...);
  messages.push_back(std::string(severity) + ":" + origin + ":" + message);
}
#define ENABLE_SERIAL_LOG
#define LOG_LEVEL 2
#define LOG_DBG(origin, format, ...) record("DBG", origin, format, ##__VA_ARGS__)
#define LOG_ERR(origin, format, ...) record("ERR", origin, format, ##__VA_ARGS__)
#define LOG_INF(origin, format, ...) record("INF", origin, format, ##__VA_ARGS__)
// ACTUAL_CSS_DIAGNOSTIC
struct IoContext {
  struct Owner {
    size_t ruleCount() const { return 0; }
  } owner;
  static constexpr uint8_t CSS_CACHE_VERSION = XP_CSS_CACHE_VERSION;
  static constexpr size_t READ_BUFFER_SIZE = XP_CSS_READ_BUFFER_BYTES;
  // ACTUAL_TRANSACTION_DIAGNOSTIC
};

#include "physical_defaults.h"

extern "C" {
using Inspect = uint8_t (*)(void*, const uint8_t*, const uint8_t*, size_t, uint8_t);
uint8_t xp_epub_test_scratch(const XpAppResources*, const XpEpubLeaves*, uint8_t, Inspect, void*);
void xp_test_fail_allocation_after(size_t);
size_t xp_test_live_allocations();
}
alignas(std::max_align_t) static uint8_t block[50000];
static int releases = 0, claims = 0, closes = 0;
static size_t nativeLive = 0, nativeFailAfter = SIZE_MAX;
static void* scratchMalloc(size_t bytes) {
  if (nativeFailAfter == 0) return nullptr;
  if (nativeFailAfter != SIZE_MAX) --nativeFailAfter;
  void* result = std::malloc(bytes);
  if (result) ++nativeLive;
  return result;
}
static void scratchFree(void* bytes) {
  if (bytes) --nativeLive;
  std::free(bytes);
}
namespace HalMemory {
struct Unexpected {
  uint8_t* release() { std::abort(); }
};
inline Unexpected allocatePsram(size_t) { std::abort(); }
struct PsramDeleter {
  void operator()(uint8_t*) { std::abort(); }
};
}  // namespace HalMemory
#define malloc scratchMalloc
#define free scratchFree
// ACTUAL_CSTR_HELPER
#define HalFile HostFile
#include <HalStorage.h>
#undef HalFile
static size_t liveFiles = 0, born = 0, closeCalls = 0, closeFailures = 0, renames = 0;
static bool failClose = false;
static std::vector<std::string> removed;
struct HalFile : HostFile {
  bool wrote = false;
  HalFile() {
    ++liveFiles;
    ++born;
  }
  ~HalFile() {
    --liveFiles;
    ++closes;
  }
  size_t write(const uint8_t* bytes, size_t length) {
    wrote = true;
    return HostFile::write(bytes, length);
  }
  bool close() {
    ++closeCalls;
    const bool ok = HostFile::close();
    if (wrote && failClose) {
      ++closeFailures;
      return false;
    }
    return ok;
  }
};
static const XpAppResources resources = [] {
  XpAppResources value = unexpectedAppResources();
  // ACTUAL_ALLOCATOR_LEAVES
  return value;
}();
#undef malloc
#undef free
static bool lent = false, expectBorrow = false, failAfterInspection = false, rejectSink = false, reclaimEarly = false;
struct BookMetadataCache {
  static void lockReadCursor() {}
  static void unlockReadCursor() {}
};
// ACTUAL_TEXT_HELPERS
static const XpEpubLeaves leaves = [] {
  XpEpubLeaves value = unexpectedEpubLeaves();
  // ACTUAL_LEAVES
  return value;
}();

extern "C" uint64_t xp_epub_test_css(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText, uint8_t,
                                     uint8_t);
extern "C" void xp_epub_test_css_diagnostic(const XpEpubLeaves*, uint32_t, uint32_t, uint32_t);
extern "C" void xp_epub_test_css_transaction_diagnostic(const XpAppResources*, const XpEpubLeaves*, uint32_t, uint32_t);
static std::vector<uint8_t> readBytes(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}
static void put(const std::string& path, const std::string& bytes) {
  std::ofstream file(path, std::ios::binary);
  file << bytes;
}
int main(int argc, char** argv) {
  assert(argc == 2);
  for (uint32_t event = 1; event <= 13; ++event) {
    for (uint32_t value : {0u, 1u, 65535u, 2147483647u}) {
      messages.clear();
      cssDiagnostic(nullptr, event, value, 123, 0, 0);
      const auto expected = messages;
      messages.clear();
      xp_epub_test_css_diagnostic(&leaves, event, value, 123);
      assert(messages == expected);
    }
  }
  const std::string root = argv[1], source = root + "/source.css", cache = root + "/cache";
  for (uint32_t event = 0; event <= 18; ++event) {
    for (uint32_t value : {0u, 1u, 65535u, 2147483647u}) {
      messages.clear();
      IoContext context;
      IoContext::diagnostic(&context, event, value);
      const auto expected = messages;
      messages.clear();
      xp_epub_test_css_transaction_diagnostic(&resources, &leaves, event, value);
      assert(messages == expected);
    }
  }
  std::filesystem::create_directories(cache);
  put(source, ".a{font-weight:bold}");
  const std::string final = cache + "/css_rules.cache", backup = final + ".bak", temporary = final + ".tmp";
  auto run = [&](uint8_t kind, bool complete = true) {
    const auto result = xp_epub_test_css(&resources, &leaves, text(cache), text(source), kind, complete);
    assert(nativeLive == 0 && xp_test_live_allocations() == 0 && liveFiles == 0 && born == size_t(closes));
    return result;
  };
  assert(leaves.file_close(nullptr) == 0);
  assert(run(0) == (uint64_t(1) << 32 | 1));
  const auto previous = readBytes(final);
  assert(previous.size() > 4 && previous[0] == 19 && previous[1] == 0);
  assert(run(2) == 1 && run(1) == uint64_t(1) << 32);
  put(backup, "previous backup");
  const auto previousBackup = readBytes(backup);
  put(source, ".b{font-style:italic}");
  failClose = true;
  renames = 0;
  removed.clear();
  messages.clear();
  const size_t beforeClose = closeCalls, beforeBorn = born;
  assert(run(0) == uint64_t(1) << 32);
  failClose = false;
  assert(closeFailures == 1 && renames == 0);
  assert(closeCalls - beforeClose == born - beforeBorn);
  assert(readBytes(final) == previous && readBytes(backup) == previousBackup);
  assert(!std::filesystem::exists(temporary));
  assert(removed == (std::vector<std::string>{temporary, temporary}));
  assert(messages == (std::vector<std::string>{"DBG:CSS:Parsed 1 rules from 21 bytes",
                                               "ERR:CSS:Failed to write temporary CSS cache"}));
  assert(run(0, false) == (uint64_t(1) << 32 | 1));
  const auto partial = readBytes(final);
  assert(partial[0] == 19 && partial[1] == 1 && !std::filesystem::exists(backup));
  assert(run(2) == 2 && run(1) == uint64_t(1) << 32);
  std::filesystem::rename(final, backup);
  assert(run(4) == 1 && readBytes(final) == partial);
  assert(run(3) == 0 && !std::filesystem::exists(final));
  put(final, std::string(1, '\x12'));
  assert(run(1) == 2 && !std::filesystem::exists(final));
  for (size_t fail = 0; fail < 4; ++fail) {
    nativeFailAfter = fail;
    run(0);
    nativeFailAfter = SIZE_MAX;
  }
  for (size_t fail = 0; fail < 18; ++fail) {
    xp_test_fail_allocation_after(fail);
    run(0);
    xp_test_fail_allocation_after(SIZE_MAX);
  }
  put(source, "/* no rules */");
  assert(run(0) == 1);
  assert(readBytes(final) == (std::vector<uint8_t>{19, 0, 0, 0}));
  assert(born == closeCalls);
}
