#include "Logging.h"

#include <BoardConfig.h>
#include <esp_cpu.h>
#include <esp_rom_sys.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

#include <string>

#include "BoundedLog.h"
#include "LogCriticalSection.h"
#include "LogHeadroom.h"

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
  loglite::Journal journal{};
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
  if (task && state.journal.ownedBy(reinterpret_cast<uintptr_t>(task))) return nullptr;
  if (task && state.render.task == task) return &state.render;
  if (task && state.loop.task == task) return &state.loop;
  return nullptr;
}

__attribute__((noinline)) void commitLine(const char* line, bool defer) {
  const size_t length = strnlen(line, MAX_ENTRY_LEN - 1);
  loglite::CriticalSection lock;
  state.journal.commit(logMessages, line, length, logHead, rtcLogMagic, defer);
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

__attribute__((noinline)) void drainOwned() {
  char line[MAX_ENTRY_LEN];
  while (true) {
    bool copied;
    {
      loglite::CriticalSection lock;
      copied = state.journal.copyNext(logMessages, line, logHead);
    }
    if (!copied) break;
    writeLine(line);
  }
  uint32_t dropped, waits, timeouts;
  {
    loglite::CriticalSection lock;
    dropped = state.journal.takeDropped();
    state.journal.takeContention(waits, timeouts);
  }
  if (dropped) {
    loglite::formatCounter(line, dropped, " lines dropped\n");
    writeLine(line);
  }
  if (waits) {
    loglite::formatCounter(line, waits, " contention waits\n");
    writeLine(line);
  }
  if (timeouts) {
    loglite::formatCounter(line, timeouts, " contention timeouts\n");
    writeLine(line);
  }
}

__attribute__((noinline)) void finishLine(const char* line) {
  const auto task = xTaskGetCurrentTaskHandle();
  bool allowed = task && !xPortInIsrContext() && xPortCanYield() && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
#if CONFIG_IDF_TARGET_ARCH_RISCV
  allowed = allowed && (RV_READ_CSR(mstatus) & MSTATUS_MIE);
#endif
  if (!allowed) {
    commitLine(line, true);
    return;
  }
  const auto base = reinterpret_cast<uintptr_t>(pxTaskGetStackStart(nullptr));
  const size_t loopBytes = task == state.loop.task ? getArduinoLoopTaskStackSize() : 0;
  const auto sp = reinterpret_cast<uintptr_t>(esp_cpu_get_sp());
  if (!loglite::emissionFits(base, sp, loopBytes)) {
    commitLine(line, true);
    return;
  }
  loglite::DrainClaim claim;
  {
    loglite::CriticalSection lock;
    claim = state.journal.beginDrain(reinterpret_cast<uintptr_t>(task));
  }
  if (claim == loglite::DrainClaim::Busy) {
    // Match HWCDC's 1 ms tx_lock wait, configured in setup(); never wait masked.
    constexpr auto waitTicks = pdMS_TO_TICKS(1);
    if (waitTicks) {
      {
        loglite::CriticalSection lock;
        state.journal.noteWait();
      }
      vTaskDelay(waitTicks);
    }
    loglite::CriticalSection lock;
    claim = state.journal.beginDrain(reinterpret_cast<uintptr_t>(task));
    if (claim == loglite::DrainClaim::Busy) state.journal.noteTimeout();
  }
  switch (claim) {
    case loglite::DrainClaim::Acquired:
      // Free a full FIFO before committing; preserve this writer's task scratch.
      drainOwned();
      commitLine(line, true);
      {
        loglite::CriticalSection lock;
        state.journal.snapshotPending();
      }
      drainOwned();
      {
        loglite::CriticalSection lock;
        state.journal.endDrain();
      }
      return;
    case loglite::DrainClaim::Busy:
    case loglite::DrainClaim::Reentrant:
      commitLine(line, true);
      return;
  }
}

__attribute__((noinline)) void immediateLog(const char* level, const char* origin, const char* format, va_list args) {
  char buf[MAX_ENTRY_LEN];
  if (!legacyFormat(buf, millis(), level, origin, format, args)) return;
  finishLine(buf);
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
  finishLine(capture->line);
}

#ifdef ENABLE_SERIAL_LOG
void drainLogs() {
  const auto task = xTaskGetCurrentTaskHandle();
  {
    loglite::CriticalSection lock;
    if (state.journal.beginDrain(reinterpret_cast<uintptr_t>(task)) != loglite::DrainClaim::Acquired) return;
  }
  drainOwned();
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
