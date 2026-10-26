#include <PackageParser.h>
#include <Utf8.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <string>

#include "Epub/BookMetadataCache.h"

namespace {
struct alignas(std::max_align_t) Allocation {
  size_t size;
};
size_t liveBytes = 0;
size_t peakBytes = 0;
bool failNothrowArray = false;
size_t failures = 0;
void* allocate(size_t size) {
  auto* header = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
  if (!header) return nullptr;
  header->size = size;
  liveBytes += size;
  peakBytes = std::max(peakBytes, liveBytes);
  return header + 1;
}
void release(void* ptr) {
  if (!ptr) return;
  auto* header = static_cast<Allocation*>(ptr) - 1;
  liveBytes -= header->size;
  std::free(header);
}
void check(bool result) {
  if (!result) std::abort();
}
bool feed(Print& parser, const std::string& text) {
  for (size_t pos = 0; pos < text.size(); pos += 512) {
    const size_t len = std::min(size_t{512}, text.size() - pos);
    if (parser.write(reinterpret_cast<const uint8_t*>(text.data() + pos), len) != len) return false;
  }
  return true;
}
std::string read(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}
void appendString(std::string& out, const std::string& value) {
  const uint32_t len = value.size();
  out.append(reinterpret_cast<const char*>(&len), sizeof(len));
  out.append(value);
}
void run(bool nav, size_t size, bool compose, bool fail, const std::string& root) {
  std::string label(size, 'a');
  if (compose) label.replace(size - 3, 3, "e\xcc\x81");
  const std::string prefix = nav ? "<html><body><nav type=\"toc\"><ol><li><a href=\"x\">" + label
                                 : "<ncx><navMap><navPoint><navLabel><text>" + label + "</text></navLabel>";
  const std::string suffix =
      nav ? "</a></li></ol></nav></body></html>" : "<content src=\"x\"/></navPoint></navMap></ncx>";
  std::string expected;
  appendString(expected, utf8ComposeNfc(label));
  appendString(expected, "x");
  appendString(expected, "");
  expected.push_back(1);
  expected.append(2, '\0');
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  BookMetadataCache cache(root);
  check(cache.beginWrite() && cache.beginContentOpfPass());
  cache.createSpineEntry("x");
  check(cache.endContentOpfPass() && cache.beginTocPass());
  const std::string base;
  PackageParser navParser(XP_PKG_NAV_PASS, base, prefix.size() + suffix.size(), "", &cache);
  PackageParser ncxParser(XP_PKG_NCX_PASS, base, prefix.size() + suffix.size(), "", &cache);
  Print& parser = nav ? static_cast<Print&>(navParser) : static_cast<Print&>(ncxParser);
  check(nav ? navParser.setup() : ncxParser.setup());
  check(feed(parser, prefix));
  const size_t before = liveBytes;
  peakBytes = liveBytes;
  failNothrowArray = fail;
  const bool ok = feed(parser, suffix);
  failNothrowArray = false;
  const size_t peak = peakBytes;
  check(ok != fail);
  check(cache.getTocCount() == (fail ? 0 : 1));
  check(cache.endTocPass() && cache.endWrite());
  check(read(root + "/toc.bin.tmp") == (fail ? std::string{} : expected));
  if (fail) {
    const uint8_t byte = ' ';
    check(parser.write(&byte, 1) == 0);
  }
  std::cout << (nav ? "nav" : "ncx") << ',' << size << ',' << compose << ',' << fail << ',' << before << ',' << peak
            << ',' << peak - before << '\n';
  std::filesystem::remove_all(root);
}
}  // namespace

void* operator new(size_t size) {
  if (void* p = allocate(size)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* ptr) noexcept { release(ptr); }
void operator delete[](void* ptr) noexcept { release(ptr); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  if (failNothrowArray) {
    ++failures;
    return nullptr;
  }
  return allocate(size);
}
void operator delete[](void* ptr, const std::nothrow_t&) noexcept { release(ptr); }
extern "C" void* heap_caps_malloc(size_t size, uint32_t) {
  if (failNothrowArray && size == 32767) {
    ++failures;
    return nullptr;
  }
  return allocate(size);
}
extern "C" void* heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t) {
  check(alignment <= alignof(std::max_align_t));
  return allocate(size);
}
extern "C" void heap_caps_free(void* ptr) { release(ptr); }
extern "C" void xp_chapter_release_block(void*) { std::abort(); }
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  std::cout << "parser,label_bytes,compose,fail,live_before_emit,peak_emit,extra_emit\n";
  for (bool nav : {false, true}) {
    for (size_t size : {8192, 32768, 65536}) {
      for (bool compose : {false, true}) {
        const size_t before = liveBytes;
        run(nav, size, compose, false, argv[1]);
        check(liveBytes == before);
      }
    }
#ifndef TOC_BOUNDARY_BASELINE
    const size_t before = liveBytes;
    run(nav, 32768, true, true, argv[1]);
    check(liveBytes == before);
#endif
  }
#ifndef TOC_BOUNDARY_BASELINE
  check(failures == 2);
#endif
}
