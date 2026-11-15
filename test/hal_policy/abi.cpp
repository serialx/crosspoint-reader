#include <xp_hal.h>

#include <cassert>
#include <cstdio>
#include <cstring>

static_assert(sizeof(XpHalSecret) == 33);
static_assert(sizeof(XpHalLight) == 2);
static_assert(sizeof(XpHalTilt) == 24);
static_assert(sizeof(XpHalClock) == 16);
static_assert(sizeof(XpHalCalendar) == 8);
static_assert(sizeof(XpHalPower) == 8);
static_assert(sizeof(XpHalBattery) == 8);
static_assert(sizeof(XpHalInput) == 4);

int main() {
  assert(!xp_hal_secret(nullptr, nullptr, nullptr));
  assert(!xp_hal_tilt_wake(nullptr, nullptr));
  assert(!xp_hal_tilt_sleep(nullptr, nullptr));
  assert(!xp_hal_power_lock(nullptr, nullptr));
  assert(!xp_hal_clock_read(nullptr, nullptr, nullptr));
  assert(!xp_hal_clock_sync(nullptr, nullptr));
  assert(!xp_hal_usb_begin(nullptr));
  assert(xp_hal_read_string(nullptr, 10) == 0);
  assert(!xp_hal_time_wait(0, 0));
  assert(xp_hal_time_wait(5, 0xfffffff0));
  assert(!xp_hal_time_wait(0xfffffff0, 5));
  for (unsigned hour = 0; hour < 24; ++hour) {
    for (unsigned minute = 0; minute < 60; ++minute) {
      char expected[16], actual[16];
      snprintf(expected, sizeof(expected), "%02u:%02u", hour, minute);
      assert(xp_hal_clock_format(actual, sizeof(actual), false, hour, minute));
      assert(strcmp(expected, actual) == 0);
      snprintf(expected, sizeof(expected), "%u:%02u %s", hour % 12 ? hour % 12 : 12, minute, hour >= 12 ? "PM" : "AM");
      assert(xp_hal_clock_format(actual, sizeof(actual), true, hour, minute));
      assert(strcmp(expected, actual) == 0);
    }
  }
}
