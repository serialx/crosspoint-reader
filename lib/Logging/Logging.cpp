#include "Logging.h"

#include <BoardConfig.h>
#include <esp_rom_sys.h>
#include <freertos/task.h>

#include <string>

#include "BoundedLog.h"
#include "LogCriticalSection.h"

#define MAX_ENTRY_LEN 256
#define MAX_LOG_LINES 16

// Simple ring buffer log, useful for error reporting when we encounter a crash
RTC_NOINIT_ATTR char logMessages[MAX_LOG_LINES][MAX_ENTRY_LEN];
RTC_NOINIT_ATTR size_t logHead = 0;
// Magic word written alongside logHead to detect uninitialized RTC memory.
// RTC_NOINIT_ATTR is not zeroed on cold boot, so logHead may appear in-range
// (0..MAX_LOG_LINES-1) by chance even though logMessages is garbage. The magic
// value is only set by clearLastLogs(), so its absence means the buffer was
// never properly initialized.
RTC_NOINIT_ATTR uint32_t rtcLogMagic;
static constexpr uint32_t LOG_RTC_MAGIC = 0xDEADBEEF;

namespace {
struct TaskCapture {
  TaskHandle_t task = nullptr;
  char line[MAX_ENTRY_LEN]{};
};
struct LoggerState {
  TaskCapture render;
  TaskCapture loop;
  loglite::Journal journal{logMessages, logHead, rtcLogMagic};
};
constinit LoggerState state;
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(state) <= 544);
#endif

__attribute__((noinline)) TaskCapture* currentCapture() {
  // An ISR borrows the interrupted task's handle, but must not borrow its scratch.
  if (xPortInIsrContext()) return nullptr;
  const auto task = xTaskGetCurrentTaskHandle();
  loglite::CriticalSection lock;
  if (task && state.render.task == task) return &state.render;
  if (task && state.loop.task == task) return &state.loop;
  return nullptr;
}

__attribute__((noinline)) void commitLine(const char* line, bool defer) {
  const size_t length = strnlen(line, MAX_ENTRY_LEN - 1);
  loglite::CriticalSection lock;
  state.journal.commit(line, length, defer);
}

void writeLine(const char* line) {
#if FREEINK_LOG_TRANSPORT == FREEINK_LOG_TRANSPORT_ROM_PRINTF
  esp_rom_printf("%s", line);
#else
  // Queue even during a SOF-watchdog disconnect so the TX interrupt is rearmed.
  logSerial.print(line);
#endif
}

__attribute__((noinline)) bool legacyFormat(char (&buf)[MAX_ENTRY_LEN], unsigned long ms, const char* level,
                                            const char* origin, const char* format, va_list args) {
  const int prefix = snprintf(buf, sizeof(buf), "[%lu] [%s] [%s] ", ms, level, origin);
  if (prefix < 0) return false;
  const size_t used = std::min(static_cast<size_t>(prefix), sizeof(buf) - 1);
  return vsnprintf(buf + used, sizeof(buf) - used, format, args) >= 0;
}

__attribute__((noinline)) void immediateLog(const char* level, const char* origin, const char* format, va_list args) {
  char buf[MAX_ENTRY_LEN];
  if (!legacyFormat(buf, millis(), level, origin, format, args)) return;
  writeLine(buf);
  commitLine(buf, false);
}
}  // namespace

#ifdef ENABLE_SERIAL_LOG
void registerLogRenderTask() {
  const auto task = xTaskGetCurrentTaskHandle();
  loglite::CriticalSection lock;
  state.render.task = task;
}

void registerLogLoopTask() {
  const auto task = xTaskGetCurrentTaskHandle();
  loglite::CriticalSection lock;
  state.loop.task = task;
}
#endif

void addToLogRingBuffer(const char* message) { commitLine(message, false); }

// Since logging can take a large amount of flash, we want to make the format string as short as possible.
// This logPrintf prepend the timestamp, level and origin to the user-provided message, so that the user only needs to
// provide the format string for the message itself.
void logPrintf(const char* level, const char* origin, const char* format, ...) {
  va_list args;
  va_start(args, format);
  auto* capture = currentCapture();
  if (!capture) {
    immediateLog(level, origin, format, args);
    va_end(args);
    return;
  }
  const auto ms = millis();
  if (!loglite::formatLine(capture->line, ms, level, origin, format, args) &&
      !legacyFormat(capture->line, ms, level, origin, format, args)) {
    va_end(args);
    return;
  }
  va_end(args);
  commitLine(capture->line, true);
}

#ifdef ENABLE_SERIAL_LOG
void drainLogs() {
  {
    loglite::CriticalSection lock;
    if (!state.journal.beginDrain()) return;
  }
  char line[MAX_ENTRY_LEN];
  while (true) {
    bool copied;
    {
      loglite::CriticalSection lock;
      copied = state.journal.copyNext(line);
    }
    if (!copied) break;
    writeLine(line);
  }
  uint32_t dropped;
  {
    loglite::CriticalSection lock;
    dropped = state.journal.takeDropped();
  }
  if (dropped) {
    loglite::formatDropped(line, dropped);
    writeLine(line);
  }
  loglite::CriticalSection lock;
  state.journal.endDrain();
}
#endif

std::string getLastLogs() {
  if (rtcLogMagic != LOG_RTC_MAGIC) {
    return {};
  }
  std::string output;
  for (size_t i = 0; i < MAX_LOG_LINES; i++) {
    size_t idx = (logHead + i) % MAX_LOG_LINES;
    if (logMessages[idx][0] != '\0') {
      const size_t len = strnlen(logMessages[idx], MAX_ENTRY_LEN);
      output.append(logMessages[idx], len);
    }
  }
  return output;
}

// Checks whether the RTC log state is consistent: rtcLogMagic must equal
// LOG_RTC_MAGIC and logHead must be in 0..MAX_LOG_LINES-1. Returns true if
// corruption is detected, in which case rtcLogMagic is still invalid and
// logMessages may contain garbage. Callers (e.g. HalSystem::begin on the
// panic-reboot path) must call clearLastLogs() after a true result to fully
// reinitialize the ring buffer and stamp the magic before getLastLogs() is used.
bool sanitizeLogHead() {
  loglite::CriticalSection lock;
  if (rtcLogMagic != LOG_RTC_MAGIC || logHead >= MAX_LOG_LINES) {
    logHead = 0;
#ifdef ENABLE_SERIAL_LOG
    state.journal.clearPending();
#endif
    return true;
  }
  return false;
}

void clearLastLogs() {
  loglite::CriticalSection lock;
  for (size_t i = 0; i < MAX_LOG_LINES; i++) {
    logMessages[i][0] = '\0';
  }
  logHead = 0;
  rtcLogMagic = LOG_RTC_MAGIC;
#ifdef ENABLE_SERIAL_LOG
  state.journal.clearPending();
#endif
}
