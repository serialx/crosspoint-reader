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
bool formatLine(char (&destination)[LineSize], uint32_t milliseconds, const char* level, const char* origin,
                const char* format, va_list args) noexcept;
void formatCounter(char (&destination)[LineSize], uint32_t count, const char* event) noexcept;
enum class DrainClaim { Acquired, Busy, Reentrant };

// Callers supply the RTC ring and serialize every operation; no transport runs here.
class Journal {
 public:
  constexpr Journal() noexcept = default;
  Journal(const Journal&) = delete;
  Journal& operator=(const Journal&) = delete;
  void commit(char (&lines)[LineCount][LineSize], const char* line, size_t length, size_t& head, uint32_t& magic,
              bool defer) noexcept;
  void clearPending() noexcept;
  DrainClaim beginDrain(uintptr_t task) noexcept;
  bool ownedBy(uintptr_t task) const noexcept { return owner == task; }
  void snapshotPending() noexcept { snapshot = pending; }
  bool copyNext(char (&lines)[LineCount][LineSize], char (&destination)[LineSize], size_t head) noexcept;
  void noteWait() noexcept { ++waits; }
  void noteTimeout() noexcept { ++timeouts; }
  void takeContention(uint32_t& waitCount, uint32_t& timeoutCount) noexcept;
  void endDrain() noexcept;
  uint32_t takeDropped() noexcept;

 private:
  uint32_t dropped = 0;
  uint16_t pending = 0;
  uint16_t snapshot = 0;
  uintptr_t owner = 0;
  uint32_t waits = 0;
  uint32_t timeouts = 0;
};
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(Journal) == 20);
#endif
}  // namespace loglite
