#pragma once
#include <Arduino.h>
enum { ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_GPIO, ESP_SLEEP_WAKEUP_EXT1, ESP_SLEEP_WAKEUP_TIMER };
enum { ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_DEEPSLEEP, ESP_RST_OTHER };
inline int wakeCause = 0, resetCause = 0;
inline int esp_sleep_get_wakeup_cause() { puts("wake cause"); return wakeCause; }
inline int esp_reset_reason() { puts("reset cause"); return resetCause; }
