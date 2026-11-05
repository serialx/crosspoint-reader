#include <Epub/Page.h>
#include <xp_epub_session.h>

#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "../reader_lifecycle/legacy_page_metadata.h"
#include "../reader_lifecycle/physical_defaults.h"
#include "PageViews.h"

extern "C" {
uint8_t xp_epub_test_page_owner(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText, uint32_t,
                                void (*)(void*, uint8_t, uint16_t, const uint8_t*, size_t), void*);
void xp_epub_test_image_scale(const XpEpubLeaves*, int32_t, int32_t, uint32_t);
}
using Bytes = std::vector<uint8_t>;
static std::vector<std::string> logs;
static XpAppResources resources = appTestResources();
static XpEpubLeaves leaves = unexpectedEpubLeaves();
static size_t failAt = SIZE_MAX, requests = 0, liveBytes = 0;
static std::array<std::pair<void*, size_t>, 4> live{};
static std::vector<size_t> sizes;
static Bytes fileBytes;
static uint32_t filePosition;
static size_t ioCount, ioFail = SIZE_MAX;
static bool fileOpen;
static size_t cases, pages, scales;

void pageOwnerLog(const char* tag, const char* format, ...) {
  char text[256];
  va_list args;
  va_start(args, format);
  std::vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  logs.push_back(std::string(tag) + ":" + text);
}
// Fill the native arena with a nonzero pattern before its decoder initializes it.
void* operator new(size_t size, const std::nothrow_t&) noexcept {
  void* p = std::malloc(size);
  if (p) std::memset(p, 0xa5, size);
  return p;
}
void* operator new[](size_t size, const std::nothrow_t& tag) noexcept { return ::operator new(size, tag); }

static uint8_t* allocate(XpAppMemoryDomain domain, size_t bytes) {
  assert(domain == XP_APP_MEMORY_DEFAULT);
  sizes.push_back(bytes);
  if (requests++ == failAt) return nullptr;
  auto* p = static_cast<uint8_t*>(std::malloc(bytes));
  assert(p);
  std::memset(p, 0x5a, bytes);
  for (auto& entry : live) {
    if (!entry.first) {
      entry = {p, bytes};
      liveBytes += bytes;
      return p;
    }
  }
  std::abort();
}
static void release(XpAppMemoryDomain domain, uint8_t* p) {
  assert(domain == XP_APP_MEMORY_DEFAULT);
  for (auto& entry : live) {
    if (entry.first == p) {
      liveBytes -= entry.second;
      entry = {};
      std::free(p);
      return;
    }
  }
  std::abort();
}
static void reset() {
  assert(liveBytes == 0 && !fileOpen);
  requests = ioCount = 0;
  failAt = ioFail = SIZE_MAX;
  sizes.clear();
  logs.clear();
}
static void initialize() {
  resources.buffer_allocate = allocate;
  resources.buffer_release = release;
  leaves.log_enabled = [](uint8_t) -> uint8_t { return 1; };
  leaves.log = [](uint8_t, XpAppText tag, XpAppText text) {
    logs.push_back(std::string(reinterpret_cast<const char*>(tag.data), tag.len) + ":" +
                   std::string(reinterpret_cast<const char*>(text.data), text.len));
  };
  // Only physical file operations are substituted; Rust FileSlot and Page are real.
  leaves.file_open_read = [](XpAppText, XpAppText) -> void* {
    assert(!fileOpen);
    fileOpen = true;
    filePosition = 0;
    return &filePosition;
  };
  leaves.file_close = [](void* handle) -> uint8_t {
    assert(handle == &filePosition && fileOpen);
    fileOpen = false;
    return 1;
  };
  leaves.file_position = [](void*) -> uint32_t { return filePosition; };
  leaves.file_size = [](void*) -> uint32_t { return fileBytes.size(); };
  leaves.file_seek = [](void*, uint32_t position) -> uint8_t {
    // The fixture's initial seek is outside the Page load fault schedule.
    if (filePosition == 0 && position == 4) {
      filePosition = position;
      return 1;
    }
    if (ioCount++ == ioFail || position > fileBytes.size()) return 0;
    filePosition = position;
    return 1;
  };
  leaves.file_read = [](void*, uint8_t* output, uint32_t bytes) -> int32_t {
    if (ioCount++ == ioFail) return -1;
    bytes = std::min<uint32_t>(bytes, fileBytes.size() - filePosition);
    std::memcpy(output, fileBytes.data() + filePosition, bytes);
    filePosition += bytes;
    return bytes;
  };
}
struct Expected {
  XpPageInfo info;
  const uint8_t* arena;
  const Page* native;
  PageMetadata metadata;
  size_t records = 0;
};
static void observe(void* raw, uint8_t kind, uint16_t index, const uint8_t* bytes, size_t length) {
  auto& e = *static_cast<Expected*>(raw);
  if (kind == 0) {
    assert(length == e.info.arena_size);
    assert(std::memcmp(bytes, e.arena, length) == 0);
    assert(liveBytes == length + 2 * sizeof(void*));
    assert(sizes[0] == liveBytes);
    Page borrowed(Page::Borrowed{}, bytes, length, e.native->visibleTextOffset);
    comparePage(*e.native, borrowed);
    ++pages;
    return;
  }
  assert(liveBytes == e.info.footnotes * 288u + e.info.links * 264u);
  if (kind == 1 || kind == 2) {
    const auto& note = e.metadata.footnotes()[index];
    assert(length == (kind == 1 ? 32 : 256));
    assert(std::memcmp(bytes, kind == 1 ? note.number : note.href, length) == 0);
  } else {
    const auto& link = e.metadata.links()[index];
    if (kind == 3) {
      assert(length == 256 && std::memcmp(bytes, link.href, 256) == 0);
    } else {
      const int16_t values[]{link.x, link.y, link.width, link.height};
      assert(length == 2);
      assert(static_cast<int16_t>(bytes[0] | uint16_t(bytes[1]) << 8) == values[kind - 4]);
    }
  }
  ++e.records;
}
static void check(const Bytes& bytes, uint32_t visible, bool faultCases) {
  reset();
  auto native = Page::fromBytes(bytes.data(), bytes.size(), visible);
  const auto parentLogs = logs;
  XpPageInfo info{};
  if (native) assert(!xp_page_arena_size(bytes.data(), bytes.size(), &info));
  // Page::allocate places its arena immediately after the actual native header.
  Expected expected{
      info, native ? reinterpret_cast<const uint8_t*>(native.get()) + sizeof(Page) : nullptr, native.get(), {}};
  if (native) assert(expected.metadata.assign(*native));
  for (bool file : {false, true}) {
    reset();
    fileBytes.assign(4, 0xcc);
    fileBytes.insert(fileBytes.end(), bytes.begin(), bytes.end());
    const uint8_t name[] = "page";
    expected.records = 0;
    const auto result = xp_epub_test_page_owner(&resources, &leaves, {bytes.data(), bytes.size()},
                                                {name, file ? 4u : 0u}, visible, observe, &expected);
    assert(bool(result) == bool(native));
    assert(result == 0 || result == 2);
    if (result == 2) assert(expected.records == info.footnotes * 2u + info.links * 5u);
    assert(liveBytes == 0 && !fileOpen);
    // File probing can reject earlier with its own admission diagnostic.
    if (!file) assert(logs == parentLogs);
    if (native) {
      assert(logs.empty());
      const size_t completed = requests, operations = ioCount;
      if (faultCases) {
        for (size_t fail = 0; fail < completed; ++fail) {
          reset();
          failAt = fail;
          const auto failure = xp_epub_test_page_owner(&resources, &leaves, {bytes.data(), bytes.size()},
                                                       {name, file ? 4u : 0u}, visible, observe, &expected);
          assert(failure == (fail == 0 ? 0 : 1));
          assert(liveBytes == 0 && !fileOpen && logs.size() == 1);
          if (fail == 0)
            assert(logs[0] == "PGE:Deserialization failed: could not allocate Page");
          else {
            const bool note = sizes[fail] == info.footnotes * 288u;
            assert(logs[0] ==
                   "PGE:OOM: " + std::string(note ? "footnotes " : "links ") + std::to_string(sizes[fail]) + " bytes");
          }
        }
        if (file)
          for (size_t fail = 0; fail < operations; ++fail) {
            reset();
            ioFail = fail;
            assert(xp_epub_test_page_owner(&resources, &leaves, {bytes.data(), bytes.size()}, {name, 4}, visible,
                                           observe, &expected) == 0);
            assert(liveBytes == 0 && !fileOpen);
          }
      }
    }
    ++cases;
  }
}
static void scaleTests(const char* path) {
  // The immutable oracle is recorded from both pinned target libcs in QEMU.
  // Host libc is not the reference: macOS drops the sign of negative NaNs.
  std::ifstream input(path);
  assert(input);
  std::string row;
  while (std::getline(input, row)) {
    assert(row.size() > 9 && row[8] == '\t');
    const auto bits = static_cast<uint32_t>(std::stoul(row.substr(0, 8), nullptr, 16));
    const auto expected = "EHP:Display size: -23x456 (scale " + row.substr(9) + ")";
    logs.clear();
    xp_epub_test_image_scale(&leaves, -23, 456, bits);
    assert(logs.size() == 1);
    if (logs[0] != expected) {
      std::cerr << std::hex << bits << ": " << expected << " != " << logs[0] << '\n';
      std::abort();
    }
    ++scales;
  }
  assert(input.eof() && scales == 125326);
}
int main(int argc, char** argv) {
  initialize();
  if (argc > 1 && std::string(argv[1]) == "--scale") {
    assert(argc == 3);
    scaleTests(argv[2]);
    std::cout << "scale cases: " << scales << '\n';
    return 0;
  }
  // Full metadata record caps, independent of ordinary text-page fixtures.
  uint8_t storage[16000]{};
  XpPageBuilder builder{};
  assert(!xp_page_builder_init(&builder, storage, sizeof(storage)));
  const uint8_t number[32] = "1", href[256] = "#n";
  const XpPageLink link{{href, 256}, -2, 7, 13, 19};
  for (unsigned i = 0; i < 16; ++i) assert(!xp_page_builder_footnote(&builder, number, href));
  for (unsigned i = 0; i < 32; ++i) assert(!xp_page_builder_link(&builder, &link));
  size_t length;
  assert(!xp_page_builder_finish(&builder, &length));
  check({storage, storage + length}, 321, true);
  const Bytes empty(6, 0);
  check(empty, 0, true);
  for (size_t n = 0; n < empty.size(); ++n) check(Bytes(n, 0), 0, false);
  if (argc > 1) {
    std::ifstream in(argv[1], std::ios::binary);
    uint32_t size, visible;
    size_t index = 0;
    while (in.read(reinterpret_cast<char*>(&size), 4)) {
      assert(in.read(reinterpret_cast<char*>(&visible), 4));
      Bytes bytes(size);
      assert(in.read(reinterpret_cast<char*>(bytes.data()), size));
      check(bytes, visible, index++ < 10);
      if (index <= 3) {
        for (size_t cut = 0; cut < bytes.size(); ++cut) check({bytes.begin(), bytes.begin() + cut}, visible, false);
        for (size_t at = 0; at < bytes.size(); ++at) {
          auto changed = bytes;
          changed[at] ^= 0x80;
          check(changed, visible, false);
        }
      }
    }
    assert(in.eof());
  }
  std::cout << "{\"cases\":" << cases << ",\"accepted_pages\":" << pages << ",\"scale_cases\":" << scales << "}\n";
}
