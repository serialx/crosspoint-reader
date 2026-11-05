#pragma once
#include <Arduino.h>
inline bool rtcBegin = true, rtcRead = true, rtcWrite = true;
struct Rtc {
  struct DateTime {
    uint16_t year;
    uint8_t month, day, hour, minute, second, weekday;
  };
  inline static DateTime value{2026, 10, 29, 13, 15, 37, 4};
  bool begin() {
    puts("rtc begin");
    return rtcBegin;
  }
  bool now(DateTime& d) {
    puts("rtc now");
    d = value;
    return rtcRead;
  }
  bool set(const DateTime& d) {
    printf("rtc set %u %u %u %u %u %u %u\n", d.year, d.month, d.day, d.hour, d.minute, d.second, d.weekday);
    value = d;
    return rtcWrite;
  }
};
