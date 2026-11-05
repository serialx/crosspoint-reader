#include "HalPowerManager.h"

#include <BoardConfig.h>
#include <Logging.h>
#include <PowerManager.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <soc/soc_caps.h>

#include <cassert>

#include "HalGPIO.h"

#if FREEINK_DEVICE_EEGO_A4 || FREEINK_DEVICE_PICCO
#include <HalFrontlight.h>
#endif

#if FREEINK_DEVICE_PICCO
#include <driver/gpio.h>
#endif

#if FREEINK_DEVICE_PAPERMONO
#include <M5Pm1.h>
#endif

HalPowerManager powerManager;  // Singleton instance

// GPIO13 controls the X4 battery latch and the X3 SD power rail on the C3
// Xteink boards. Other boards use it for unrelated signals, including the
// X4 Pro display chip select.
static constexpr gpio_num_t XTEINK_C3_GPIO13 = GPIO_NUM_13;

void HalPowerManager::begin() {
  if (BoardConfig::ACTIVE.batteryAdc >= 0) {
    pinMode(BoardConfig::ACTIVE.batteryAdc, INPUT);
  }
  // Board charger setup (Picco SGM41562: stock charge voltage and limits); a
  // no-op on boards without a configurable charger.
  if (BatteryMonitor::configureCharger()) LOG_INF("PWR", "Charger configured");
  state.normal_freq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  assert(modeMutex != nullptr);
}

XpHalPowerServices HalPowerManager::services() {
  return {this,
          [](void* ctx, XpHalPowerOp op, int32_t value) -> int32_t {
            auto* self = static_cast<HalPowerManager*>(ctx);
            switch (op) {
              case XP_HAL_POWER_WIFI:
                return WiFi.getMode() != WIFI_MODE_NULL;
              case XP_HAL_POWER_LOCK:
                xSemaphoreTake(self->modeMutex, portMAX_DELAY);
                return 0;
              case XP_HAL_POWER_UNLOCK:
                xSemaphoreGive(self->modeMutex);
                return 0;
              case XP_HAL_POWER_FREQUENCY:
                return setCpuFrequencyMhz(value);
            }
            return 0;
          },
          [](void*, uint32_t event, uint32_t frequency, uint32_t, uint32_t, uint32_t) {
            switch (event) {
              case 0:
                LOG_DBG("PWR", "Going to low-power mode");
                break;
              case 1:
                LOG_DBG("PWR", "Restoring normal CPU frequency");
                break;
              case 2:
                LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", static_cast<int>(frequency));
                break;
              case 3:
                LOG_ERR("PWR", "Lock already held, ignore");
                break;
            }
          },
          LOW_POWER_FREQ};
}
void HalPowerManager::setPowerSaving(bool enabled) {
  const auto s = services();
  xp_hal_power_set(&state, &s, enabled);
}

void HalPowerManager::startDeepSleep(HalGPIO& gpio, const bool wakeOnHeldButton) const {
#if FREEINK_DEVICE_EEGO_A4 || FREEINK_DEVICE_PICCO
  // The frontlight driver and touch share I2C; turn the light off before touch
  // releases the bus. On Picco this also puts the TMA525C to sleep.
  Frontlight.setOn(false);
  gpio.prepareForDeepSleep();
#endif
#ifdef ENABLE_SERIAL_LOG
  // Tear down HWCDC so the host sees a clean disconnect and the peripheral
  // doesn't hold power domains that interfere with USB-powered GPIO wake.
  // logSerial is the raw HWCDC reference; Serial is the MySerialImpl proxy
  // (which doesn't expose end()).
  logSerial.end();
#endif

#if !SOC_PM_SUPPORT_EXT1_WAKEUP
  if (gpio.isXteinkDevice()) {
    // GPIO13 gates the battery MOSFET on both Xteink C3 boards; driving it low
    // is the battery power-off (the SDK wake source still handles USB power).
    // Release any surviving pad hold first: hold_en survives deep sleep via
    // the SDK's deepSleep() (esp_sleep_config_gpio_isolate +
    // gpio_deep_sleep_hold_en), and a held pad silently ignores the drive.
    gpio_hold_dis(XTEINK_C3_GPIO13);
    gpio_set_direction(XTEINK_C3_GPIO13, GPIO_MODE_OUTPUT);
    gpio_set_level(XTEINK_C3_GPIO13, 0);
    gpio_hold_en(XTEINK_C3_GPIO13);
  }
#endif

  // Hold every configured power-latch pin HIGH through deep sleep. These are
  // keep-alive enables (the X4 Pro's master peripheral rail on GPIO1, the
  // Sticky's PWR_HOLD/PWR_LOCK): deepSleep() isolates all pads
  // (esp_sleep_config_gpio_isolate), so a latch without an armed hold loses its
  // output driver and floats — on the X4 Pro the latch drops as soon as
  // external power leaves (serial/pogo adapter unplugged), and the next power-
  // button press cold-boots instead of fast-waking. holdPowerRails() asserted
  // the latches at boot but arms no sleep hold; arm it here instead. Skips
  // XTEINK_C3_GPIO13: it IS power.latch0 on the C3 Xteink boards, where the
  // block above drives it LOW on purpose (battery power-off).
  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin < 0 || static_cast<gpio_num_t>(pin) == XTEINK_C3_GPIO13) continue;
    const auto g = static_cast<gpio_num_t>(pin);
    // Release any surviving pad hold first: a held pad silently ignores the
    // drive below (same trap as the GPIO13 block above).
    gpio_hold_dis(g);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    gpio_hold_en(g);
  }

  // Cut the gated peripheral rails (touch/SD/EPD on boards like the Sticky) and
  // hold the enables off through deep sleep — otherwise the GT911 and SD card
  // stay powered all through "off" and drain the battery. No-op on boards with
  // no switched rails (X4/X3). Trade-off: no touch-to-wake; wake is the power
  // button. Must run after display.deepSleep() so the panel controller gets its
  // deep-sleep command while its rail is still up (enterDeepSleep() in main.cpp
  // guarantees that ordering).
  freeink::PowerManager::powerDownRailsForSleep();

#if FREEINK_DEVICE_PAPERMONO
  // Its power button is behind the M5PM1 PMIC rather than an ESP GPIO, so
  // normal GPIO deep sleep would have no wake source. Ask the PMIC to shut the
  // device down; a button click then restarts it through a cold boot.
  if (freeink::m5pm1::requestShutdown()) {
    delay(1000);  // allow the PMIC firmware time to drop power
  }
#endif
#if FREEINK_DEVICE_METALIO_EINK4
  // Deep sleep leaves the board, 4G modem and audio module powered, so cut
  // power instead; the button cold-boots it. Returns only on USB power.
  freeink::metalio::powerOff();
#endif

  if (wakeOnHeldButton) {
    freeink::PowerManager::armPowerButtonWakeup();
    freeink::PowerManager::deepSleep();
  }
  // Waits for the power button to be physically released (so holding it doesn't
  // immediately wake the device again), then arms the wake source and sleeps.
  freeink::PowerManager::deepSleepUntilPowerButton();
}

#if FREEINK_DEVICE_PICCO
// ponytail: 30 s poll; the SGM41562 status is I2C-only, so USB unplug and
// charge progress are only seen when the timer fires.
static constexpr uint64_t USB_HOLD_POLL_US = 30ULL * 1000 * 1000;

bool HalPowerManager::holdWhileUsbPowered(HalGPIO& gpio, void (*onStatus)(bool charging, uint16_t percent)) const {
  if (!gpio.isUsbConnected()) return false;
  LOG_DBG("PWR", "USB power present, holding in light sleep");
  const auto powerPin = static_cast<gpio_num_t>(BoardConfig::ACTIVE.input.power);
  // The press that requested sleep may still be down; it must not end the hold.
  while (digitalRead(powerPin) == LOW) delay(10);  // Picco power button is active-low
  const BatteryMonitor battery;
  int lastCharging = -1;
  int lastPercent = -1;
  while (gpio.isUsbConnected()) {
    const bool charging = battery.isCharging();
    const uint16_t percent = getBatteryPercentage();
    if (static_cast<int>(charging) != lastCharging || percent != lastPercent) {
      lastCharging = charging;
      lastPercent = percent;
      onStatus(charging, percent);
    }
    esp_sleep_enable_timer_wakeup(USB_HOLD_POLL_US);
    gpio_wakeup_enable(powerPin, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();
    gpio_wakeup_disable(powerPin);
    // gpio_wakeup_disable() keeps the LOW_LEVEL type, and a later pinMode() would
    // enable it as an interrupt that storms while the button is held.
    gpio_set_intr_type(powerPin, GPIO_INTR_DISABLE);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (digitalRead(powerPin) == LOW) return true;
  }
  return false;
}
#endif

uint16_t HalPowerManager::getBatteryPercentage() const {
  static const BatteryMonitor battery;
  const XpHalBatteryServices services{nullptr, []() { return static_cast<uint32_t>(millis()); },
                                      [](void*, uint16_t* percent) { return battery.readPercentageChecked(*percent); },
                                      [](void*) { return battery.readPercentage(); }};
  return xp_hal_power_battery(&batteryState, &services, BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0);
}

HalPowerManager::Lock::Lock() {
  const auto s = powerManager.services();
  valid = xp_hal_power_lock(&powerManager.state, &s);
}

HalPowerManager::Lock::~Lock() {
  const auto s = powerManager.services();
  xp_hal_power_unlock(&powerManager.state, &s, valid);
}
