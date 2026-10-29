#pragma once
#include <Arduino.h>
constexpr int SNTP_SYNC_STATUS_COMPLETED = 1;
inline bool esp_sntp_enabled() { puts("sntp enabled"); return syncEnabled; }
inline int sntp_get_sync_status() { printf("sntp status %d\n", syncPolls); return syncPolls++ >= syncAfter; }
inline void sntp_set_time_sync_notification_cb(void (*)(timeval*)) { puts("sntp callback"); }
