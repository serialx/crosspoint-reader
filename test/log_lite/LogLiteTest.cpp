#include <BoundedLog.h>
#include <LogHeadroom.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

static void require(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

static void formats(char* out, size_t size, const char* format, ...) {
  va_list args;
  va_start(args, format);
  const bool ok = loglite::format(out, size, format, args);
  va_end(args);
  require(ok, "supported format rejected");
}

static char rtc[loglite::LineCount][loglite::LineSize]{};
static size_t head;
static uint32_t magic;
static bool record(loglite::Journal& journal, const char* text, ...) {
  va_list args;
  va_start(args, text);
  char line[loglite::LineSize];
  const bool result = loglite::formatLine(line, 42, "ERR", "FDC", text, args);
  if (result) journal.commit(rtc, line, std::strlen(line), head, magic, true);
  va_end(args);
  return result;
}

static void rejectedFormat(const char* pattern, ...) {
  char output[] = "untouched";
  va_list args;
  va_start(args, pattern);
  require(!loglite::format(output, sizeof(output), pattern, args), "unsupported suffix was formatted");
  require(va_arg(args, unsigned) == 123u, "preflight consumed arguments before rejecting");
  va_end(args);
  require(std::strcmp(output, "untouched") == 0, "rejection partially changed output");
}

static void journalTests() {
  head = magic = 0;
  loglite::Journal journal;
  require(record(journal, "Failed to allocate %u bytes for hot group %u\n", 100u, 2u), "capture rejected");
  require(std::strcmp(rtc[0], "[42] [ERR] [FDC] Failed to allocate 100 bytes for hot group 2\n") == 0,
          "line not retained before transport");
  require(head == 1 && magic == loglite::RtcMagic, "RTC head/magic not committed");
  // Reset ordinary state without clearing the RTC region, as on panic reboot.
  loglite::Journal afterReset;
  require(afterReset.beginDrain(1) == loglite::DrainClaim::Acquired, "reset replayed an old crash line");
  char resetCopy[loglite::LineSize];
  require(!afterReset.copyNext(rtc, resetCopy, head), "reset replayed an old crash line");
  afterReset.endDrain();
  require(std::strstr(rtc[0], "hot group 2") != nullptr, "reset lost committed diagnostic");
  require(journal.beginDrain(1) == loglite::DrainClaim::Acquired, "pending record missing");
  require(journal.beginDrain(1) == loglite::DrainClaim::Reentrant, "second drainer acquired an active stream");
  char copy[loglite::LineSize];
  require(journal.copyNext(rtc, copy, head), "pending copy missing");
  require(std::strcmp(copy, rtc[0]) == 0, "transport copy differs from crash line");
  require(!journal.copyNext(rtc, copy, head), "same record drained twice");
  journal.endDrain();
  for (unsigned i = 0; i < 20; ++i) require(record(journal, "line %u", i), "burst rejected");
  require(journal.takeDropped() == 4, "overflow did not count four overwritten pending lines");
  require(journal.takeDropped() == 0, "overflow count repeated");
  require(journal.beginDrain(1) == loglite::DrainClaim::Acquired, "overflow lost all pending lines");
  for (unsigned i = 4; i < 20; ++i) {
    char expected[80];
    std::snprintf(expected, sizeof(expected), "[42] [ERR] [FDC] line %u", i);
    require(journal.copyNext(rtc, copy, head) && std::strcmp(copy, expected) == 0,
            "wraparound reordered retained lines");
  }
  require(!journal.copyNext(rtc, copy, head), "drain exceeded retained lines");
  journal.endDrain();
  require(record(journal, "old") && journal.beginDrain(1) == loglite::DrainClaim::Acquired, "snapshot setup failed");
  require(record(journal, "new"), "interleaved capture failed");
  require(journal.copyNext(rtc, copy, head) && std::strstr(copy, "old"), "snapshot lost original record");
  require(!journal.copyNext(rtc, copy, head), "new record extended an active snapshot");
  journal.endDrain();
  require(journal.beginDrain(1) == loglite::DrainClaim::Acquired && journal.copyNext(rtc, copy, head) &&
              std::strstr(copy, "new"),
          "next pass lost new record");
  journal.endDrain();
  require(record(journal, "snapshot") && journal.beginDrain(1) == loglite::DrainClaim::Acquired,
          "overwrite snapshot setup failed");
  for (unsigned i = 0; i < 16; ++i) require(record(journal, "replacement %u", i), "replacement capture failed");
  require(!journal.copyNext(rtc, copy, head), "overwritten snapshot slot admitted a new record");
  journal.endDrain();
  require(journal.beginDrain(1) == loglite::DrainClaim::Acquired, "replacement records lost");
  for (unsigned i = 0; i < 16; ++i) require(journal.copyNext(rtc, copy, head), "replacement record missing");
  require(!journal.copyNext(rtc, copy, head), "snapshot did not stop at sixteen");
  journal.endDrain();
  const size_t savedHead = head;
  require(!record(journal, "unsupported %f", 1.0) && head == savedHead, "unsupported capture committed a record");
  head = 999;
  require(record(journal, "recovered") && head == 1, "invalid RTC head indexed outside ring");
  require(journal.beginDrain(1) == loglite::DrainClaim::Acquired && journal.copyNext(rtc, copy, head) &&
              std::strstr(copy, "recovered"),
          "recovery lost line");
  journal.endDrain();
  magic = 0;
  require(record(journal, "cold") && head == 1 && rtc[1][0] == '\0', "cold RTC retained garbage records");
}

int main(int argc, char** argv) {
  if (argc == 2) return loglite::supports(argv[1]) ? 0 : 1;
  // Losing a sign, padding or a length modifier corrupts failure diagnostics.
  char out[256];
  formats(out, sizeof(out), "%s %d %u %04X %02X %lu %zu %%", "failure", INT_MIN, UINT_MAX, 10u, 255u, ULONG_MAX,
          SIZE_MAX);
  char native[256];
  std::snprintf(native, sizeof(native), "%s %d %u %04X %02X %lu %zu %%", "failure", INT_MIN, UINT_MAX, 10u, 255u,
                ULONG_MAX, SIZE_MAX);
  require(std::strcmp(out, native) == 0, "integer and string bytes differ from printf");
  formats(out, sizeof(out), "[%8.3s][%-6s][%.*s][%.*s]", "abcdef", "xy", 2, "hello", -1, "whole");
  require(std::strcmp(out, "[     abc][xy    ][he][whole]") == 0, "string width/precision changed");
  formats(out, sizeof(out), "[%08d][%.0u][%8.4x]", -12, 0u, 10u);
  require(std::strcmp(out, "[-0000012][][    000a]") == 0, "numeric padding or zero precision changed");
  formats(out, sizeof(out), "%p", static_cast<void*>(nullptr));
  require(std::strcmp(out, "0x0") == 0, "C3 pointer spelling changed");
  formats(out, sizeof(out), "%p", reinterpret_cast<void*>(uintptr_t{0x1234}));
  require(std::strcmp(out, "0x1234") == 0, "pointer digits changed");
  for (const char* bad : {"%f", "%llu", "%n", "%ls", "%", "%#x", "%+d", "%*u"})
    require(!loglite::supports(bad), "unsupported format admitted");
  char guarded[6] = {'L', '?', '?', '?', '?', 'R'};
  formats(guarded + 1, 4, "%s", "abcdef");
  require(std::memcmp(guarded, "Labc\0R", 6) == 0, "truncation overwrote adjacent memory");
  formats(out, 1, "%s", "text");
  require(out[0] == '\0', "one-byte output is not terminated");
  formats(nullptr, 0, "%d", 123);
  formats(out, sizeof(out), "%s", "한글");
  require(std::strcmp(out, "한글") == 0, "UTF-8 bytes changed");
  formats(out, 4, "%8s", "abcdefghij");
  require(std::strcmp(out, "abc") == 0, "long string was replaced by width padding");
  require(!loglite::supports("%999999999999999999s"), "unbounded string-width scan admitted");
  formats(out, 5, "%999999999999999999999u", 1u);
  require(std::strcmp(out, "    ") == 0, "large width is not bounded by destination");
  rejectedFormat("%u then %f", 123u, 1.0);
  formats(out, sizeof(out), "[%.*s]", 0, static_cast<const char*>(nullptr));
  require(std::strcmp(out, "[]") == 0, "zero precision read or emitted a null string");
  for (size_t capacity = 0; capacity <= sizeof(out); ++capacity) {
    char actual[258];
    char expected[258];
    std::memset(actual, '@', sizeof(actual));
    std::memset(expected, '@', sizeof(expected));
    formats(actual + 1, capacity, "[%013ld] %zu %8.3s %04X %.*s", LONG_MIN, SIZE_MAX, "abcdef", 65535u, 6, "abcdefghi");
    std::snprintf(expected + 1, capacity, "[%013ld] %zu %8.3s %04X %.*s", LONG_MIN, SIZE_MAX, "abcdef", 65535u, 6,
                  "abcdefghi");
    require(std::memcmp(actual, expected, sizeof(actual)) == 0, "capacity sweep differs from native printf");
  }
  constexpr uintptr_t base = 0x10000;
  constexpr auto render = loglite::DrainReserve + loglite::RenderReserve;
  require(!loglite::emissionFits(base, base + render - 1), "render guard admitted one byte short");
  require(loglite::emissionFits(base, base + render), "render guard rejected exact threshold");
  require(loglite::emissionFits(base, base + render + 1), "render guard rejected spare byte");
  require(!loglite::emissionFits(base, base - 1), "stack subtraction underflow admitted emission");
  for (size_t bytes : {size_t{16384}, size_t{24576}}) {
    const auto need = loglite::DrainReserve + bytes - loglite::LoopGate + 16;
    require(!loglite::emissionFits(base, base + need - 1, bytes), "loop admitted below reserve");
    require(loglite::emissionFits(base, base + need, bytes), "loop rejected threshold");
  }
  journalTests();
  return 0;
}
