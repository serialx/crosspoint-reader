#include <BuildScratch.h>
#include <Memory.h>
#include <xp_epub_session.h>

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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
// ACTUAL_INFLATE_OOM
static std::vector<std::string> zipMessages;
template <class... Args>
static void recordZip(const char* severity, const char* origin, const char* format, Args... arguments) {
  char message[512]{};
  std::snprintf(message, sizeof(message), format, arguments...);
  const auto value = std::string(severity) + ":" + origin + ":" + message;
  zipMessages.push_back(value);
  inflateEvents.push_back(value);
}
#define ENABLE_SERIAL_LOG
#define LOG_LEVEL 2
#define LOG_DBG(origin, format, ...) recordZip("DBG", origin, format, ##__VA_ARGS__)
#define LOG_ERR(origin, format, ...) recordZip("ERR", origin, format, ##__VA_ARGS__)
#define LOG_INF(origin, format, ...) recordZip("INF", origin, format, ##__VA_ARGS__)
// ACTUAL_EXTRACT_ENUM
struct ExtractContext {
  const char* filename;
};
static const auto nativeExtractDiagnostic =  // ACTUAL_EXTRACT_DIAGNOSTIC
    ;
#define malloc scratchMalloc
#define free scratchFree
static const XpAppResources resources = [] {
  XpAppResources value = unexpectedAppResources();
  // ACTUAL_ALLOCATOR_LEAVES
  return value;
}();
#undef malloc
#undef free
static bool lent = false, expectBorrow = false, failAfterInspection = false, rejectSink = false, reclaimEarly = false;
#define HalFile HostFile
#include <HalStorage.h>
#undef HalFile
struct HalFile : HostFile {
  ~HalFile() { ++closes; }
};
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

extern "C" int64_t xp_epub_test_zip(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText, uint8_t,
                                    XpBookSink, void*);
extern "C" void xp_epub_test_zip_diagnostic(const XpEpubLeaves*, XpAppText, uint32_t, uint32_t, uint32_t);
static std::vector<uint8_t> output;
static bool reject = false;
static uint8_t receive(void*, const uint8_t* bytes, size_t size) {
  output.insert(output.end(), bytes, bytes + size);
  return !reject;
}
static std::vector<uint8_t> readBytes(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), {}};
}
int main(int argc, char** argv) {
  assert(argc == 3);
  const std::string path = argv[1];
  const auto expected = readBytes(argv[2]);
  std::string name = "OPS/body.xhtml";
  for (const std::string filename : {std::string("OPS/\xff.xhtml"), std::string("OPS/name\0ignored", 16)}) {
    ExtractContext context{filename.c_str()};
    for (uint32_t event = 1; event <= 15; ++event) {
      for (uint32_t value : {0u, 1u, 0x7fffffffu, 0xffffffffu}) {
        zipMessages.clear();
        nativeExtractDiagnostic(&context, event, value, 123, 0, 0);
        const auto wanted = zipMessages;
        zipMessages.clear();
        xp_epub_test_zip_diagnostic(&leaves, text(filename), event, value, 123);
        assert(zipMessages == wanted);
      }
    }
  }
  auto run = [&](uint8_t kind) {
    output.clear();
    const auto result = xp_epub_test_zip(&resources, &leaves, text(path), text(name), kind, receive, nullptr);
    assert(nativeLive == 0 && xp_test_live_allocations() == 0);
    auto* probe = buildscratch::claim(1);
    if (lent) {
      assert(probe == block);
      buildscratch::release(probe);
    } else
      assert(!probe);
    return result;
  };
  assert(run(0) == static_cast<int64_t>(expected.size()));
  name.append("\0ignored", 8);
  assert(run(0) == static_cast<int64_t>(expected.size()));
  assert(run(5) == 1 && output == expected);
  name = "OPS/body.xhtml";
  for (bool loan : {false, true}) {
    if (loan) {
      buildscratch::lend(block, sizeof(block));
      lent = true;
    }
    assert(run(1) == 1 && output == expected);
    assert(run(5) == 1 && output == expected);
    auto terminated = expected;
    terminated.push_back(0);
    assert(run(6) == 1 && output == terminated);
    name = "OPS/empty.xhtml";
    assert(run(5) == 1 && output.empty());
    assert(run(6) == 1 && output == std::vector<uint8_t>{0});
    name = "OPS/body.xhtml";
    const int opens = Storage.readOpens;
    assert(run(4) == 1 && output == expected);
    assert(Storage.readOpens == opens + 1);
    reject = true;
    assert(run(1) == 0);
    assert(run(2) == 1);
    reject = false;
    if (loan) {
      buildscratch::reclaim();
      lent = false;
    }
  }
  for (uint8_t kind : {1, 5, 6}) {
    for (size_t fault = 0; fault < 7; ++fault) {
      nativeFailAfter = fault;
      inflateEvents.clear();
      const auto result = run(kind);
      for (size_t index = 0; index < inflateEvents.size(); ++index) {
        if (inflateEvents[index].find("ZIP:Inflate ") == 0) {
          assert(index >= 2 && index + 1 < inflateEvents.size());
          assert(inflateEvents[index - 2].find("default:") == 0);
          assert(inflateEvents[index - 1].find("psram:") == 0);
          assert(inflateEvents[index + 1] == "ERR:ZIP:Failed to init inflate stream for OPS/body.xhtml");
        }
      }
      if (result) {
        auto wanted = expected;
        if (kind == 6) wanted.push_back(0);
        assert(output == wanted);
      } else if (kind != 1) {
        assert(output.empty());
      }
      nativeFailAfter = SIZE_MAX;
    }
  }
  HalFileTest::allowSeek = [](const char*, size_t) { return false; };
  assert(run(1) == 0);
  assert(run(5) == 0 && output.empty());
  HalFileTest::allowSeek = nullptr;
  HalFileTest::readLimit = [](const char*, size_t, size_t) -> size_t { return 0; };
  assert(run(1) == 0);
  assert(run(6) == 0 && output.empty());
  HalFileTest::readLimit = nullptr;
  assert(closes == Storage.readOpens);
}
