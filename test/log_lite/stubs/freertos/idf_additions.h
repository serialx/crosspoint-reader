#pragma once
#include <esp_cpu.h>
#include <freertos/task.h>
inline uint8_t* pxTaskGetStackStart(TaskHandle_t) { return reinterpret_cast<uint8_t*>(stackBase); }
