#pragma once
#include <cstdio>
using SemaphoreHandle_t = void*;
constexpr unsigned portMAX_DELAY = 0xffffffffu;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { puts("mutex create"); return (void*)1; }
inline void xSemaphoreTake(SemaphoreHandle_t, unsigned) { puts("mutex take"); }
inline void xSemaphoreGive(SemaphoreHandle_t) { puts("mutex give"); }
