#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <sys/time.h>
inline uint32_t ticks = 0;
inline int64_t clockNow = 0;
inline bool syncEnabled = false;
inline int syncAfter = 0;
inline int syncPolls = 0;
inline uint32_t millis() { printf("millis %u\n", ticks); return ticks; }
inline void delay(uint32_t ms) { printf("delay %u\n", ms); ticks += ms; }
inline void configTzTime(const char* tz, const char* a, const char* b = nullptr) {
  printf("sntp %s %s %s\n", tz, a, b ? b : "-"); syncEnabled = true; setenv("TZ", tz, 1); tzset();
}
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock), printf("lock\n"))
#define portEXIT_CRITICAL(lock) ((void)(lock), printf("unlock\n"))
inline time_t testTime(time_t*) { printf("time %lld\n", (long long)clockNow); return clockNow; }
inline int testSetTime(const timeval* tv, const void*) { clockNow = tv->tv_sec; printf("settime %lld\n", (long long)clockNow); return 0; }
#define time testTime
#define settimeofday testSetTime
inline bool cpuOk = true;
inline int cpuFreq = 160;
constexpr int INPUT = 0;
inline void pinMode(int pin, int mode) { printf("pinMode %d %d\n", pin, mode); }
inline int getCpuFrequencyMhz() { puts("cpu get"); return cpuFreq; }
inline bool setCpuFrequencyMhz(int mhz) { printf("cpu set %d %u\n", mhz, cpuOk); if (cpuOk) cpuFreq = mhz; return cpuOk; }

constexpr int HIGH = 1;
inline int digitalValue = 0;
inline int digitalRead(int p) { printf("digital %d\n", p); return digitalValue; }
#include <esp_sleep.h>
