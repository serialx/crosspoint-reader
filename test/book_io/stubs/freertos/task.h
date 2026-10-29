#pragma once
#include <cstdio>
inline void vTaskDelay(unsigned ticks) { std::printf("yield %u\n", ticks); }
