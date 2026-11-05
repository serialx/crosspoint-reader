#include "TrustedTime.h"

#include <Arduino.h>
#include <Logging.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <xp_hal.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace trustedtime {

namespace {

constexpr const char* PREFS_NAMESPACE = "cptime";
constexpr const char* PREFS_KEY = "floor";

// Latest time this boot has seen (or restored from NVS). trustedNow() never
// reports earlier, so a backward clock step (a bad SNTP answer, a manual set)
// cannot reopen an expired loan. Updated from the SNTP callback's lwIP task as
// well as the main task; 64-bit atomics are not lock-free on the C3.
int64_t ramFloor = 0;
portMUX_TYPE ramFloorLock = portMUX_INITIALIZER_UNLOCKED;

// Raises the in-RAM floor to `value` if higher; returns the resulting floor.
int64_t raiseFloor(const int64_t value) {
  portENTER_CRITICAL(&ramFloorLock);
  const int64_t floor = xp_hal_time_raise(&ramFloor, value);
  portEXIT_CRITICAL(&ramFloorLock);
  return floor;
}

int64_t readFloor() {
  Preferences prefs;
  if (!prefs.begin(PREFS_NAMESPACE, /*readOnly=*/true)) return 0;
  const int64_t value = prefs.getLong64(PREFS_KEY, 0);
  prefs.end();
  return value;
}

void writeFloor(const int64_t value) {
  Preferences prefs;
  if (!prefs.begin(PREFS_NAMESPACE, /*readOnly=*/false)) return;
  prefs.putLong64(PREFS_KEY, value);
  prefs.end();
}

const XpHalTimeServices services{[]() { return static_cast<int64_t>(time(nullptr)); }, readFloor, writeFloor,
                                 raiseFloor,
                                 [](int64_t epoch) {
                                   timeval tv = {static_cast<time_t>(epoch), 0};
                                   settimeofday(&tv, nullptr);
                                 }};

// SNTP sync callback (lwIP task context; Preferences/NVS is mutex-guarded).
void onTimeSynced(struct timeval*) { note(); }

void configureSntp() {
  // SNTP uses UTC epochs regardless of the display timezone. Copy TZ before
  // configTzTime replaces the environment entry, as HalClock::syncFromNTP does.
  const char* currentTz = getenv("TZ");
  char timezone[64];
  snprintf(timezone, sizeof(timezone), "%s", currentTz ? currentTz : "UTC0");
  configTzTime(timezone, "pool.ntp.org");
}

}  // namespace

void init() {
  sntp_set_time_sync_notification_cb(&onTimeSynced);
  if (xp_hal_time_init(&services)) LOG_DBG("TIME", "Clock restored to persisted floor");
}

void note() { xp_hal_time_note(&services); }

void adopt(const int64_t epoch) { xp_hal_time_adopt(&services, epoch); }

void startSync() {
  if (esp_sntp_enabled()) return;  // running; the sync callback handles the rest
  configureSntp();
}

bool syncNow(const uint32_t timeoutMs) {
  configureSntp();
  const XpHalSyncServices sync{[]() { return sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED; },
                               []() { return static_cast<uint32_t>(millis()); }, [](uint32_t ms) { delay(ms); }};
  return xp_hal_time_sync(&services, &sync, timeoutMs);
}

int64_t trustedNow() { return xp_hal_time_now(&services); }

}  // namespace trustedtime
