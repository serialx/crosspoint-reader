#include <expat.h>
#include <xp_epub_session.h>
#include <xp_ffi.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

static std::vector<std::string> messages;
template <class... Args>
static void record(const char* level, const char* origin, const char* format, Args... arguments) {
  char message[256]{};
  const int prefix = std::snprintf(message, sizeof(message), "[1234] [%s] [%s] ", level, origin);
  std::snprintf(message + prefix, sizeof(message) - prefix, format, arguments...);
  messages.emplace_back(message);
}
#define ENABLE_SERIAL_LOG
#define LOG_LEVEL 2
#define LOG_DBG(origin, format, ...) record("DBG", origin, format, ##__VA_ARGS__)
#define LOG_ERR(origin, format, ...) record("ERR", origin, format, ##__VA_ARGS__)
#define LOG_INF(origin, format, ...) record("INF", origin, format, ##__VA_ARGS__)
// ACTUAL_PACKAGE_DIAGNOSTIC
#include "physical_defaults.h"

extern "C" {
void xp_epub_test_package_diagnostic(const XpEpubLeaves*, uint32_t, uint32_t, XpAppText, XpAppText);
void xp_epub_test_package_error(const XpEpubLeaves*, uint32_t, uint32_t, uint32_t, uint8_t, uint8_t);
void xp_test_fail_allocation_after(size_t);
size_t xp_test_live_allocations();
}
struct CacheStub {
  XpPkgIo sink() { std::abort(); }
  XpPkgCache* writer() { std::abort(); }
};
struct PackageParser {
  uint32_t kind;
  std::string_view base;
  size_t size = 0;
  bool metadataOnly = false;
  CacheStub* cache = nullptr;
  XpPkgPass* pass = nullptr;
  static uint32_t control(void*, uint32_t, uint32_t) { std::abort(); }
  static size_t read(void*, uint32_t, uint8_t*, size_t) { std::abort(); }
  static size_t write(void*, uint32_t, const uint8_t*, size_t) { std::abort(); }
  static uint8_t seek(void*, uint32_t, uint32_t) { std::abort(); }
  bool setup();
  void logError(const XpPkgError&) const;
};
// ACTUAL_PACKAGE_SETUP
// ACTUAL_PACKAGE_ERROR
static const XpEpubLeaves leaves = [] {
  auto value = unexpectedEpubLeaves();
  // ACTUAL_LOG_LEAVES
  return value;
}();
static XpAppText text(const std::string& value) {
  return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}
int main() {
  size_t cases = 0;
  const std::array paths = {std::string(), std::string("normal.xhtml"), std::string("a\xff\0tail", 7),
                            std::string(600, 'x')};
  for (uint32_t event = 0; event < 27; ++event) {
    for (uint32_t a : {0u, 1u, 2147483647u, 2147483648u, UINT32_MAX}) {
      for (const auto& path : paths) {
        const auto value = text(path);
        messages.clear();
        packageDiagnostic(nullptr, event, a, 0, 0, 0, value.data, value.len, value.data, value.len);
        const auto expected = messages;
        messages.clear();
        xp_epub_test_package_diagnostic(&leaves, event, a, value, value);
        assert(messages == expected);
        ++cases;
      }
    }
  }
  const std::array kinds = {XP_PKG_CONTAINER, XP_PKG_OPF_PASS, XP_PKG_NCX_PASS, XP_PKG_NAV_PASS, XP_PKG_ENCRYPTION};
  for (uint32_t kind = 0; kind < kinds.size(); ++kind) {
    PackageParser parser{static_cast<uint32_t>(kinds[kind])};
    for (uint32_t code = 0; code <= XML_ERROR_NOT_STARTED; ++code) {
      for (uint32_t line : {0u, 1u, UINT32_MAX}) {
        for (uint8_t buffer : {0, 1}) {
          messages.clear();
          parser.logError({code, line, buffer});
          const auto expected = messages;
          messages.clear();
          xp_epub_test_package_error(&leaves, kind, code, line, buffer, 0);
          assert(messages == expected);
          ++cases;
        }
      }
    }
    messages.clear();
    xp_test_fail_allocation_after(0);
    assert(!parser.setup());
    xp_test_fail_allocation_after(SIZE_MAX);
    const auto expected = messages;
    messages.clear();
    xp_test_fail_allocation_after(0);
    xp_epub_test_package_error(&leaves, kind, 0, 0, 0, 1);
    xp_test_fail_allocation_after(SIZE_MAX);
    assert(messages == expected);
    assert(xp_test_live_allocations() == 0);
    ++cases;
  }
  std::printf("%zu package diagnostic comparisons passed\n", cases);
}
