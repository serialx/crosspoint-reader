#include "HalClock.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

HalClock halClock;  // Singleton instance

void HalClock::begin() {
  state.available = _sdkRtc.begin();
  LOG_INF("CLK", state.available ? "SDK RTC found" : "RTC not found");
}

void HalClock::setTimezone(const char* posixTz) {
  setenv("TZ", posixTz && posixTz[0] != '\0' ? posixTz : "UTC0", 1);
  tzset();
  state.last_poll_ms = 0;  // re-derive local time under the new rule immediately
}

bool HalClock::localTime(struct tm& out) const {
  const XpHalClockServices services{&_sdkRtc,
                                    [](void* ctx, XpHalCalendar* date) {
                                      Rtc::DateTime dt;
                                      if (!static_cast<Rtc*>(ctx)->now(dt)) return false;
                                      *date = {dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second};
                                      return true;
                                    },
                                    []() { return static_cast<uint32_t>(millis()); }};
  int64_t utc;
  if (!xp_hal_clock_read(&state, &services, &utc)) return false;
  const time_t value = static_cast<time_t>(utc);
  localtime_r(&value, &out);
  return true;
}

bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
  struct tm local;
  if (!localTime(local)) return false;
  hour = static_cast<uint8_t>(local.tm_hour);
  minute = static_cast<uint8_t>(local.tm_min);
  return true;
}

bool HalClock::formatTime(char* buf, size_t bufSize, bool use12Hour) const {
  if (bufSize < (use12Hour ? 9u : 6u)) return false;
  struct tm local;
  if (!localTime(local)) return false;
  return xp_hal_clock_format(buf, bufSize, use12Hour, local.tm_hour, local.tm_min);
}

bool HalClock::syncFromNTP() {
  struct Context {
    HalClock* clock;
    char timezone[64];
    Rtc::DateTime date;
  } ctx{this, {}, {}};
  const XpHalClockSyncServices services{
      &ctx,
      [](void*) { return WiFi.status() == WL_CONNECTED; },
      [](void* raw) {
        auto& c = *static_cast<Context*>(raw);
        const char* tzBefore = getenv("TZ");
        if (tzBefore) snprintf(c.timezone, sizeof(c.timezone), "%s", tzBefore);
        configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");
      },
      []() { return sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED; },
      [](uint32_t ms) { delay(ms); },
      [](void* raw, XpHalCalendar* date) {
        auto& c = *static_cast<Context*>(raw);
        const time_t now = time(nullptr);
        struct tm local;
        gmtime_r(&now, &local);
        c.date = {static_cast<uint16_t>(local.tm_year + 1900), static_cast<uint8_t>(local.tm_mon + 1),
                  static_cast<uint8_t>(local.tm_mday),         static_cast<uint8_t>(local.tm_hour),
                  static_cast<uint8_t>(local.tm_min),          static_cast<uint8_t>(local.tm_sec),
                  static_cast<uint8_t>(local.tm_wday)};
        const auto& dt = c.date;
        *date = {dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second};
        return c.clock->_sdkRtc.set(dt);
      },
      [](void* raw) {
        auto& c = *static_cast<Context*>(raw);
        c.clock->setTimezone(c.timezone);
      },
      [](void* raw, uint32_t event, uint32_t, uint32_t, uint32_t, uint32_t) {
        const auto& dt = static_cast<Context*>(raw)->date;
        switch (event) {
          case 0:
            LOG_ERR("CLK", "WiFi not connected, cannot sync NTP");
            break;
          case 1:
            LOG_INF("CLK", "Starting NTP sync...");
            break;
          case 2:
            LOG_INF("CLK", "RTC set to %04u-%02u-%02u %02u:%02u:%02u UTC", dt.year, dt.month, dt.day, dt.hour,
                    dt.minute, dt.second);
            break;
          case 3:
            LOG_ERR("CLK", "NTP sync timed out");
            break;
        }
      }};
  return xp_hal_clock_sync(&state, &services);
}
