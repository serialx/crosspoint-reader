#pragma once
#include <Arduino.h>
enum { ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_GPIO, ESP_SLEEP_WAKEUP_EXT1, ESP_SLEEP_WAKEUP_TIMER };
enum {
  ESP_RST_UNKNOWN,
  ESP_RST_POWERON,
  ESP_RST_DEEPSLEEP,
  ESP_RST_OTHER,
  ESP_RST_PANIC,
  ESP_RST_CPU_LOCKUP,
  ESP_RST_INT_WDT,
  ESP_RST_TASK_WDT,
  ESP_RST_WDT,
  ESP_RST_BROWNOUT,
  ESP_RST_SW
};
using esp_reset_reason_t = int;
inline int wakeCause = 0, resetCause = 0;
inline int esp_sleep_get_wakeup_cause() {
  puts("wake cause");
  return wakeCause;
}
inline int esp_reset_reason() {
  puts("reset cause");
  return resetCause;
}
