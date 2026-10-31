#include <Epub/Page.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

void readerFailureDiagnostic(bool p);
extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_allocation_attempts();

static bool track = false, fail = false;
static size_t attempts = 0, requested = 0;
void* operator new(size_t n, const std::nothrow_t&) noexcept {
  if (track) {
    ++attempts;
    requested += n;
    if (fail) return nullptr;
  }
  return std::malloc(n ? n : 1);
}
void* operator new[](size_t n, const std::nothrow_t& tag) noexcept { return ::operator new(n, tag); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
int main(int argc, char** argv) {
  const bool readerMode = argc > 1 && std::strcmp(argv[1], "reader") == 0;
  const bool fileMode = readerMode || (argc > 1 && std::strcmp(argv[1], "file") == 0);
  if (argc > 1 && std::strcmp(argv[1], "reader-metadata") == 0) {
    readerFailureDiagnostic(true);
    return 0;
  }
  size_t index = 0;
  uint32_t size;
  while (std::fread(&size, sizeof(size), 1, stdin) == 1) {
    std::vector<uint8_t> bytes(size);
    uint8_t oom;
    if (std::fread(&oom, 1, 1, stdin) != 1 || std::fread(bytes.data(), 1, size, stdin) != size) return 2;
    std::printf("BEGIN %zu\n", index++);
    PageTransportTrace::reset();
    xp_test_fail_allocation_after(0);
    attempts = requested = 0;
    fail = oom;
    track = true;
    HalFile file(bytes.data(), bytes.size());
    auto page = fileMode ? Page::load(file, bytes.size()) : Page::fromBytes(bytes.data(), bytes.size());
    if (readerMode && !page) readerFailureDiagnostic(false);
    track = false;
    if (xp_test_allocation_attempts() != 0) return 4;
    std::printf("RESULT %d %zu %zu %zu %zu %zu %llu\n", bool(page), attempts, requested, PageTransportTrace::reads,
                PageTransportTrace::bytes, PageTransportTrace::seeks,
                static_cast<unsigned long long>(PageTransportTrace::hash));
  }
}
