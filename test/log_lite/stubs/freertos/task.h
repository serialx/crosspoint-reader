#pragma once

using TaskHandle_t = void*;
inline TaskHandle_t currentTask = nullptr;
inline bool inIsr = false;
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return currentTask; }
inline bool xPortInIsrContext() { return inIsr; }
