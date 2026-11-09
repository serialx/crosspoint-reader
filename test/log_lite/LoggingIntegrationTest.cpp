#include <Logging.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  std::exit(1);
}

static void nestedDrain() { drainLogs(); }

int main() {
  currentTask = reinterpret_cast<void*>(1);
  clearLastLogs();
  logPrintf("INF", "BOOT", "ready %d\n", 1);
  require(serialBytes == "[42] [INF] [BOOT] ready 1\n", "boot log was deferred");
  serialBytes.clear();
  registerLogRenderTask();
  logPrintf("ERR", "FDC", "Failed to allocate %u bytes for hot group %u\n", 100u, 2u);
  require(serialBytes.empty(), "render capture ran transport");
  const std::string retained = "[42] [ERR] [FDC] Failed to allocate 100 bytes for hot group 2\n";
  require(getLastLogs().find(retained) != std::string::npos, "capture not retained before drain");
  onWrite = nestedDrain;
  drainLogs();
  onWrite = nullptr;
  require(serialBytes == retained, "nested drainer repeated or lost a line");
  serialBytes.clear();
  logPrintf("INF", "FLOAT", "value %.2f\n", 1.25);
  require(serialBytes.empty(), "unsupported registered format ran immediate transport");
  logPrintf("INF", "ORDER", "next\n");
  drainLogs();
  require(serialBytes == "[42] [INF] [FLOAT] value 1.25\n[42] [INF] [ORDER] next\n", "fallback changed bytes/order");
  serialBytes.clear();
  currentTask = reinterpret_cast<void*>(2);
  registerLogLoopTask();
  logPrintf("INF", "LOOP", "one\n");
  currentTask = reinterpret_cast<void*>(1);
  logPrintf("INF", "RENDER", "two\n");
  inIsr = true;
  logPrintf("ERR", "ISR", "immediate\n");
  inIsr = false;
  require(serialBytes == "[42] [ERR] [ISR] immediate\n", "ISR borrowed registered task scratch");
  serialBytes.clear();
  drainLogs();
  require(serialBytes == "[42] [INF] [LOOP] one\n[42] [INF] [RENDER] two\n", "tasks lost commit order");
  serialBytes.clear();
  for (unsigned i = 0; i < 20; ++i) logPrintf("INF", "BURST", "%u\n", i);
  drainLogs();
  require(serialBytes.find("[42] [INF] [BURST] 4\n") == 0, "overflow did not drop oldest pending line");
  require(serialBytes.find("[LOG] Dropped 4 pending serial lines\n") != std::string::npos, "overflow count missing");
  serialBytes.clear();
  logPrintf("INF", "CLEAR", "pending\n");
  clearLastLogs();
  drainLogs();
  require(serialBytes.empty() && getLastLogs().empty(), "clear replayed stale pending state");
  std::string longText(300, 'x');
  logPrintf("INF", "TRUNC", "%s", longText.c_str());
  drainLogs();
  require(serialBytes.size() == 255 && getLastLogs() == serialBytes, "line cap or RTC/serial bytes differ");
  clearLastLogs();
  serialBytes.clear();
  logPrintf("INF", "PASS", "previous\n");
  // loop() drains the previous pass before invoking any next-pass application/SDK work.
  drainLogs();
  currentTask = reinterpret_cast<void*>(3);
  logPrintf("INF", "SDK", "next immediate\n");
  require(serialBytes == "[42] [INF] [PASS] previous\n[42] [INF] [SDK] next immediate\n",
          "next pass immediate output overtook the previous pass");
  currentTask = reinterpret_cast<void*>(2);
  serialBytes.clear();
  logPrintf("DBG", "MAIN", "Entering deep sleep\n");
  require(serialBytes.empty(), "sleep capture emitted before the sleep boundary");
  // The nonreturning sleep path drains rather than waiting for another loop pass.
  drainLogs();
  require(serialBytes == "[42] [DBG] [MAIN] Entering deep sleep\n", "sleep boundary lost a pending line");
  require(getLastLogs().find(serialBytes) != std::string::npos, "sleep drain removed RTC evidence");
  clearLastLogs();
  serialBytes.clear();
  logPrintf("INF", "PENDING", "overwritten\n");
  currentTask = reinterpret_cast<void*>(3);
  for (unsigned i = 0; i < 16; ++i) logPrintf("INF", "BACKGROUND", "%u\n", i);
  serialBytes.clear();
  drainLogs();
  require(serialBytes == "[LOG] Dropped 1 pending serial lines\n", "empty queue lost its overflow summary");
}
