#include <Memory.h>
#include <TxtToHtml.h>
#include <xp_epub_session.h>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../hal_fake_strict.h"
#include "physical_defaults.h"

static void* inputContext;
static int (*inputRead)(void*, uint8_t*, size_t);
static std::string inputPath;
static size_t files = 0, buffers = 0;
static size_t bufferFailAfter = SIZE_MAX, bufferAttempts = 0;
static void* bufferMalloc(size_t size) {
  assert(size == 8192);
  ++bufferAttempts;
  if (bufferFailAfter == 0) return nullptr;
  if (bufferFailAfter != SIZE_MAX) --bufferFailAfter;
  void* p = std::malloc(size);
  if (p) ++buffers;
  return p;
}
static void bufferFree(void* p) {
  if (p) --buffers;
  std::free(p);
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
#define malloc bufferMalloc
#define free bufferFree
static const XpAppResources resources = [] {
  auto value = unexpectedAppResources();
  // ACTUAL_ALLOCATORS
  return value;
}();
#undef malloc
#undef free
struct HalFile {
  HalFile(HalFile&& other) noexcept : HalFile() { *this = std::move(other); }
  HalFile& operator=(HalFile&& other) noexcept {
    if (this == &other) return *this;
    hasImpl = std::move(other.hasImpl);
    return *this;
  }
  hal_fake::Impl hasImpl;
  HalFile() { ++files; }
  ~HalFile() { --files; }
  bool close() {
    hal_fake::requireImpl(hasImpl, "close");
    return true;
  }
  size_t size() const {
    hal_fake::requireImpl(hasImpl, "size");
    return 0;
  }
  int read(uint8_t* bytes, size_t size) {
    hal_fake::requireImpl(hasImpl, "read");
    return inputRead(inputContext, bytes, size);
  }
};
struct PhysicalStorage {
  bool openFileForRead(const char* origin, const char* path, HalFile& file) {
    assert(std::strcmp(origin, "TXT") == 0 && inputPath == path);
    file.hasImpl = true;  // HalStorage installs an impl even when the open fails.
    return true;
  }
} Storage;
// ACTUAL_TEXT_HELPERS
static const XpEpubLeaves leaves = [] {
  auto value = unexpectedEpubLeaves();
  // ACTUAL_LEAVES
  value.log_enabled = [](uint8_t) -> uint8_t { return 0; };
  return value;
}();
extern "C" uint8_t xp_epub_test_txt_stream(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpBookSink, void*);
extern "C" size_t xp_test_live_allocations();
extern "C" void xp_test_fail_allocation_after(size_t);
bool TxtToHtml::stream(std::string_view filename, void* context, int (*read)(void*, uint8_t*, size_t), Print& out) {
  inputContext = context;
  inputRead = read;
  inputPath = filename;
  const XpAppText path{reinterpret_cast<const uint8_t*>(inputPath.data()), inputPath.size()};
  const bool ok = xp_epub_test_txt_stream(
      &resources, &leaves, path,
      [](void* raw, const uint8_t* bytes, size_t size) -> uint8_t {
        return static_cast<Print*>(raw)->write(bytes, size) == size;
      },
      &out);
  assert(files == 0 && buffers == 0 && xp_test_live_allocations() == 0);
  return ok;
}

int bookTxtFaults() {
  inputPath = "fault.MD";
  inputContext = nullptr;
  inputRead = [](void*, uint8_t*, size_t) { return 0; };
  const XpAppText path{reinterpret_cast<const uint8_t*>(inputPath.data()), inputPath.size()};
  auto run = [&] {
    const bool ok = xp_epub_test_txt_stream(
        &resources, &leaves, path, [](void*, const uint8_t*, size_t) -> uint8_t { return 1; }, nullptr);
    assert(files == 0 && buffers == 0 && xp_test_live_allocations() == 0);
    return ok;
  };
  for (size_t fail : {0u, 1u, 2u}) {
    bufferFailAfter = fail;
    bufferAttempts = 0;
    assert(run() == (fail == 2));
    assert(bufferAttempts == 2);
  }
  bufferFailAfter = SIZE_MAX;
  for (size_t fail = 0; fail < 4; ++fail) {
    xp_test_fail_allocation_after(fail);
    const bool ok = run();
    xp_test_fail_allocation_after(SIZE_MAX);
    assert(ok == (fail != 0));
  }
  return 0;
}
