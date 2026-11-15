#pragma once

using TaskHandle_t = void*;
inline TaskHandle_t currentTask = nullptr;
inline bool inIsr = false;
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return currentTask; }
inline bool xPortInIsrContext() { return inIsr; }
#include <freertos/FreeRTOS.h>
constexpr int taskSCHEDULER_RUNNING = 1;
constexpr int taskSCHEDULER_SUSPENDED = 2;
inline int schedulerState = taskSCHEDULER_RUNNING;
inline int xTaskGetSchedulerState() { return schedulerState; }
inline bool xPortCanYield() { return !interruptMask; }
inline unsigned delayCount = 0;
inline void (*onDelay)() = nullptr;
inline void vTaskDelay(unsigned) {
  ++delayCount;
  if (onDelay) onDelay();
}
#define pdMS_TO_TICKS(ms) (ms)
