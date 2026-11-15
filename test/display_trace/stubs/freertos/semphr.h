#pragma once
#include "FreeRTOS.h"
using SemaphoreHandle_t = void*;
SemaphoreHandle_t xSemaphoreCreateBinary();
int xSemaphoreTake(SemaphoreHandle_t, unsigned int timeout);
void xSemaphoreGiveFromISR(SemaphoreHandle_t, BaseType_t*);
