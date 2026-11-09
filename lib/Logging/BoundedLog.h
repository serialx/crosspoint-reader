#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace loglite {
// Unsupported formats leave the destination and arguments untouched.
bool supports(const char* format) noexcept;
bool format(char* destination, size_t capacity, const char* format, va_list args) noexcept;
constexpr size_t LineSize = 256;
constexpr size_t LineCount = 16;
constexpr uint32_t RtcMagic = 0xDEADBEEF;

// Borrows the existing RTC ring. Callers serialize every operation; no transport runs here.
class Journal {
 public:
  constexpr Journal(char (&lines)[LineCount][LineSize], size_t& head, uint32_t& magic) noexcept
      : lines(lines), head(head), magic(magic) {}
  Journal(const Journal&) = delete;
  Journal& operator=(const Journal&) = delete;
  bool capture(uint32_t milliseconds, const char* level, const char* origin, const char* format, va_list args) noexcept;
  bool beginDrain() noexcept;
  bool copyNext(char (&destination)[LineSize]) noexcept;
  void endDrain() noexcept;
  uint32_t takeDropped() noexcept;

 private:
  char (&lines)[LineCount][LineSize];
  size_t& head;
  uint32_t& magic;
  uint32_t dropped = 0;
  uint16_t pending = 0;
  uint16_t snapshot = 0;
  bool draining = false;
};
static_assert(sizeof(Journal) <= 96, "Leave room for locks and task handles within the 128-byte budget");
}  // namespace loglite
