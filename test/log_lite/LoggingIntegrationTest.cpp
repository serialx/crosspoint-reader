#include <Logging.h>
#include <esp_cpu.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  std::exit(1);
}
static void recursiveLog() {
  onWrite = nullptr;
  logPrintf("INF", "NEST", "recursive\n");
  drainLogs();
}
static void contender() {
  onWrite = nullptr;
  currentTask = reinterpret_cast<void*>(2);
  logPrintf("INF", "LOOP", "contender\n");
  currentTask = reinterpret_cast<void*>(1);
}
static void reset() {
  clearLastLogs();
  serialBytes.clear();
  stackFree = 16000;
}

int main() {
  currentTask = reinterpret_cast<void*>(1);
  reset();
  logPrintf("INF", "BOOT", "ready %d\n", 1);
  require(serialBytes == "[42] [INF] [BOOT] ready 1\n", "shallow boot log was deferred");
  registerLogRenderTask();
  reset();
  stackFree = 1000;
  logPrintf("ERR", "FDC", "Failed to allocate %u bytes for hot group %u\n", 100u, 2u);
  require(serialBytes.empty(), "deep capture ran transport");
  const std::string retained = "[42] [ERR] [FDC] Failed to allocate 100 bytes for hot group 2\n";
  require(getLastLogs().find(retained) != std::string::npos, "capture not retained before drain");
  stackFree = 16000;
  onWrite = recursiveLog;
  drainLogs();
  require(serialBytes == retained, "reentrant drainer emitted a newer line in the old snapshot");
  drainLogs();
  require(serialBytes == retained + "[42] [INF] [NEST] recursive\n", "recursive fallback lost its line");
  reset();
  stackFree = 1000;
  logPrintf("INF", "FLOAT", "value %.2f\n", 1.25);
  require(serialBytes.empty(), "unsupported deep format ran transport");
  stackFree = 16000;
  logPrintf("INF", "ORDER", "next\n");
  require(serialBytes == "[42] [INF] [FLOAT] value 1.25\n[42] [INF] [ORDER] next\n", "shallow line overtook fallback");
  reset();
  currentTask = reinterpret_cast<void*>(2);
  registerLogLoopTask();
  stackFree = 12000;
  logPrintf("INF", "LOOP", "one\n");
  require(serialBytes.empty(), "loop guard used physical headroom without the 8192-byte gate");
  currentTask = reinterpret_cast<void*>(1);
  stackFree = 16000;
  inIsr = true;
  logPrintf("ERR", "ISR", "queued\n");
  inIsr = false;
  globalInterruptsEnabled = false;
  logPrintf("INF", "MASK", "global\n");
  globalInterruptsEnabled = true;
  interruptMask = 1;
  logPrintf("INF", "MASK", "threshold\n");
  require(interruptMask == 1, "journal failed to restore an outer mask");
  interruptMask = 0;
  schedulerState = taskSCHEDULER_SUSPENDED;
  logPrintf("INF", "SCHED", "suspended\n");
  schedulerState = taskSCHEDULER_RUNNING;
  require(serialBytes.empty() && delayCount == 0, "unsafe context emitted or waited");
  logPrintf("INF", "RENDER", "two\n");
  require(serialBytes ==
              "[42] [INF] [LOOP] one\n[42] [ERR] [ISR] queued\n[42] [INF] [MASK] global\n"
              "[42] [INF] [MASK] threshold\n[42] [INF] [SCHED] suspended\n[42] [INF] [RENDER] two\n",
          "central FIFO changed reservation order");
  reset();
  std::string expected;
  for (unsigned i = 0; i < 48; ++i) {
    logPrintf("INF", "BURST", "%u\n", i);
    expected += "[42] [INF] [BURST] " + std::to_string(i) + "\n";
  }
  require(serialBytes == expected, "ordinary burst lost or reordered a line");
  reset();
  stackFree = 1000;
  for (unsigned i = 0; i < 16; ++i) logPrintf("INF", "FULL", "%u\n", i);
  stackFree = 16000;
  onWrite = recursiveLog;
  logPrintf("INF", "AFTER", "full\n");
  expected.clear();
  for (unsigned i = 0; i < 16; ++i) expected += "[42] [INF] [FULL] " + std::to_string(i) + "\n";
  expected += "[42] [INF] [NEST] recursive\n[42] [INF] [AFTER] full\n";
  require(serialBytes == expected, "full-queue predrain lost order or overwrote outer scratch");
  reset();
  onWrite = contender;
  logPrintf("INF", "OWNER", "first\n");
  drainLogs();
  require(delayCount == 1, "actual contention did not wait exactly once");
  require(serialBytes ==
              "[42] [INF] [OWNER] first\n[LOG] 1 contention waits\n[LOG] 1 contention timeouts\n"
              "[42] [INF] [LOOP] contender\n",
          "contention lost bytes, counts or order");
  reset();
  stackFree = 1000;
  for (unsigned i = 0; i < 20; ++i) logPrintf("INF", "DEEP", "%u\n", i);
  stackFree = 16000;
  drainLogs();
  require(serialBytes.find("[42] [INF] [DEEP] 4\n") == 0, "deep overflow did not drop oldest");
  require(serialBytes.find("[LOG] 4 lines dropped\n") != std::string::npos, "deep overflow count missing");
  reset();
  stackFree = 1000;
  logPrintf("INF", "CLEAR", "pending\n");
  clearLastLogs();
  stackFree = 16000;
  drainLogs();
  require(serialBytes.empty() && getLastLogs().empty(), "clear replayed stale pending state");
  std::string longText(300, 'x');
  logPrintf("INF", "TRUNC", "%s", longText.c_str());
  require(serialBytes.size() == 255 && getLastLogs() == serialBytes, "line cap or RTC/serial bytes differ");
  reset();
  stackFree = 1000;
  logPrintf("INF", "PASS", "previous\n");
  stackFree = 16000;
  currentTask = reinterpret_cast<void*>(3);
  logPrintf("INF", "SDK", "next immediate\n");
  require(serialBytes == "[42] [INF] [PASS] previous\n[42] [INF] [SDK] next immediate\n",
          "unregistered line overtook FIFO");
  reset();
  currentTask = reinterpret_cast<void*>(2);
  stackFree = 1000;
  logPrintf("DBG", "MAIN", "Entering deep sleep\n");
  stackFree = 16000;
  drainLogs();
  require(serialBytes == "[42] [DBG] [MAIN] Entering deep sleep\n", "sleep boundary lost a pending line");
  require(getLastLogs().find(serialBytes) != std::string::npos, "sleep drain removed RTC evidence");
}
