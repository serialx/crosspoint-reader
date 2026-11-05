#include <BatteryMonitor.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <PowerManager.h>
#include <Preferences.h>
#include <SPI.h>
#include <XteinkDetect.h>
#include <esp_sleep.h>

// Rust detection policy uses the SDK verdict as its numeric ABI.
static_assert(static_cast<uint8_t>(freeink::XteinkVerdict::X4Confirmed) == 0 &&
              static_cast<uint8_t>(freeink::XteinkVerdict::X3Confirmed) == 1 &&
              static_cast<uint8_t>(freeink::XteinkVerdict::Inconclusive) == 2);

// Global HalGPIO instance
HalGPIO gpio;

namespace {
constexpr char HW_NAMESPACE[] = "cphw";
constexpr char NVS_KEY_DEV_OVERRIDE[] = "dev_ovr";  // 0=auto, 1=x4, 2=x3
constexpr char NVS_KEY_DEV_CACHED[] = "dev_det";    // 0=unknown, 1=x4, 2=x3

uint8_t readDeviceValue(bool cached) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, true)) return 0;
  const uint8_t raw = prefs.getUChar(cached ? NVS_KEY_DEV_CACHED : NVS_KEY_DEV_OVERRIDE, 0);
  prefs.end();
  return raw;
}
void writeDeviceValue(uint8_t value) {
  Preferences prefs;
  if (!prefs.begin(HW_NAMESPACE, false)) return;
  prefs.putUChar(NVS_KEY_DEV_CACHED, value);
  prefs.end();
}
HalGPIO::DeviceType detectDeviceTypeWithFingerprint() {
  const XpHalDetectServices services{
      nullptr, readDeviceValue, writeDeviceValue,
      [](uint8_t* first, uint8_t* second) { return static_cast<uint8_t>(freeink::detectXteinkVerdict(first, second)); },
      [](void*, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t) {
        switch (event) {
          case 0:
            LOG_INF("HW", "Device override active: %s", a == 2 ? "X3" : "X4");
            break;
          case 1:
            LOG_INF("HW", "Using cached device type: %s", a == 2 ? "X3" : "X4");
            break;
          case 2:
            LOG_INF("HW", "Xteink probe scores: pass1=%u pass2=%u verdict=%u", static_cast<unsigned>(a),
                    static_cast<unsigned>(b), static_cast<unsigned>(c));
            break;
        }
      }};
  return xp_hal_detect_x3(&services) ? HalGPIO::DeviceType::X3 : HalGPIO::DeviceType::X4;
}

}  // namespace

void HalGPIO::begin() {
#if FREEINK_MCU_C3
  _deviceType = detectDeviceTypeWithFingerprint();
  BoardConfig::selectDevice(deviceIsX3() ? BoardConfig::Board::XteinkX3 : BoardConfig::Board::XteinkX4);

  // Resolve the per-batch controller before SPI owns the display pins. FreeInk
  // checks the OEM hw_calib/screenType value first, then falls back to its
  // two-pass display-bus probe. X3's facade keys panel selection off the sibling
  // board profile, so preserve a detected UC8279 through setDisplayX3().
  freeink::applyXteinkDisplayController();
  if (deviceIsX3() && BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279) {
    BoardConfig::selectDevice(BoardConfig::Board::XteinkX3Uc8279);
  }

  SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI, EPD_CS);

  if (deviceIsX4()) {
    pinMode(BAT_GPIO0, INPUT);
    pinMode(UART0_RXD, INPUT);
  }
#else
  _deviceType = DeviceType::X4;
#endif
  inputMgr.begin();
}

void HalGPIO::update() {
  inputMgr.update();
  const uint8_t pageButtons = inputMgr.capacitivePageButtonMask();
  const bool connected = isUsbConnected();
  xp_hal_input_update(&state, pageButtons, connected);
}

bool HalGPIO::wasUsbStateChanged() const { return state.usb_changed; }

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return inputMgr.isPressed(buttonIndex); }

void HalGPIO::prepareForDeepSleep() {
  if (!inputMgr.prepareForDeepSleep()) LOG_ERR("GPIO", "Failed to put touch controller to sleep");
}

bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return inputMgr.wasPressed(buttonIndex); }

bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed(); }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return inputMgr.wasReleased(buttonIndex); }

bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased(); }

bool HalGPIO::rawInputActive() {
  if (inputMgr.isPowerButtonPhysicallyPressed()) return true;
  InputManager::ButtonAdcSample g1{}, g2{};
  inputMgr.readButtonAdc(g1, g2);
  // The Xteink ladder idles at the ADC full-scale rail (~4095); every button band sits below 3900.
  return xp_hal_input_adc(g1.raw, g2.raw);
}

unsigned long HalGPIO::getHeldTime() const { return inputMgr.getHeldTime(); }

unsigned long HalGPIO::getPowerButtonHeldTime() const { return inputMgr.getPowerButtonHeldTime(); }

bool HalGPIO::hasTouch() const { return inputMgr.hasTouch(); }

bool HalGPIO::hasHomeKey() const { return BoardConfig::hasHomeKey(); }

bool HalGPIO::wasHomeKeyPressed() const { return inputMgr.wasHomeKeyPressed(); }

bool HalGPIO::wasHomeKeyTapped() const { return inputMgr.wasHomeKeyTapped(); }

bool HalGPIO::hasToggleSwitch() const { return inputMgr.hasToggleSwitch(); }

bool HalGPIO::isToggleSwitchOn() const { return inputMgr.isToggleSwitchOn(); }

bool HalGPIO::wasHomeKeyLongPressed() const { return inputMgr.wasHomeKeyLongPressed(); }

bool HalGPIO::wasTouchTap(float& nx, float& ny) const { return inputMgr.wasTouchTap(nx, ny); }

bool HalGPIO::wasTouchDown(float& nx, float& ny) const { return inputMgr.wasTouchPressedAt(nx, ny); }

bool HalGPIO::wasTouchReleased() const { return inputMgr.wasTouchReleased(); }

bool HalGPIO::isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const {
  return inputMgr.isTouchTapCandidate(nx, ny, heldMs);
}

bool HalGPIO::isTouchHeldAt(float& nx, float& ny) const { return inputMgr.isTouchHeldAt(nx, ny); }

bool HalGPIO::wasTouchLongPress(float& nx, float& ny) const { return inputMgr.wasTouchLongPress(nx, ny); }

void HalGPIO::suppressTouchContact() { inputMgr.suppressTouchContact(); }

unsigned long HalGPIO::lastTouchHeldMs() const { return inputMgr.lastTouchHeldMs(); }

bool HalGPIO::wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
  return inputMgr.wasSwipe(nxStart, nyStart, nxEnd, nyEnd);
}

bool HalGPIO::wasTouchActivity() const { return inputMgr.wasTouchActivity(); }

void HalGPIO::setSharedConfirmPowerShortPressEmitsPower(const bool enabled) {
  InputManager::setSharedConfirmPowerShortPressEmitsPower(enabled);
}

bool HalGPIO::hasEdgeSideButtons() const {
  return BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3Uc8279 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4Pro ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4Classic;
}

bool HalGPIO::isXteinkDevice() const {
  return BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3Uc8279 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4;
}

bool HalGPIO::verifyPowerButtonWakeup() {
  // M5Paper v1.1: the classic ESP32's reset-to-setup() latency exceeds a normal
  // wheel click, so a click wake is always released before this samples and
  // verification would re-sleep on every wake. Its wheel has hard external
  // pull-ups, so the ghost-wake debounce this implements is not needed.
  if (BoardConfig::isPaperMono() || BoardConfig::isM5PaperV11() || BoardConfig::ACTIVE.input.power < 0) {
    return true;
  }

  const XpHalWakeServices services{
      &inputMgr,
      [](void* ctx) { return static_cast<InputManager*>(ctx)->isPowerButtonPhysicallyPressed(); },
      [](void* ctx) { return static_cast<InputManager*>(ctx)->isDebouncePending(); },
      [](void* ctx) { static_cast<InputManager*>(ctx)->update(); },
      []() { return static_cast<uint32_t>(millis()); },
      [](uint32_t ms) { delay(ms); }};
  return xp_hal_verify_wake(&services);
}

bool HalGPIO::isUsbConnected() const {
  if (!deviceIsX3() && BoardConfig::ACTIVE.usbDetect >= 0) {
    return digitalRead(BoardConfig::ACTIVE.usbDetect) == HIGH;
  }
  // X3 uses GPIO20 for I2C, not USB detection. Boards without a digital
  // USB-detect line (e.g. Sticky, whose PWR_IN_VOLT is an analog
  // divider): infer external power from charging state instead. BatteryMonitor
  // picks the board's best source — charger IC status, gauge Current() sign, or
  // a /STAT pin — and reports false on boards with no battery telemetry at all.
  // A charger IC that reports its input rail (BQ25896, SGM41562, M5 PMIC)
  // answers directly; otherwise infer from charging state, where charge
  // termination at 100% reads as "not connected".
  static const BatteryMonitor battery;
  bool known = false;
  const bool present = battery.isExternalPowerPresent(&known);
  if (known) return present;
  return battery.isCharging();
}

bool HalGPIO::coldBootImpliesPowerButton() const {
  // Xteink-style power topology: the power button energizes the rail until
  // firmware latches it, so a no-USB POWERON can only be a still-held button
  // boot, and plugging USB into an off device should charge-sleep, not boot.
  // Everything else boots on any cold boot: boards with no USB detection at
  // all (M5Paper v1.1, PaperColor, Murphy, de-link) would misread USB and
  // post-flash boots as battery button boots, and STAT-only boards like the
  // EEGO A4 misread them the same way once the charger terminates at 100%
  // (STAT inactive reads as "no USB").
  // The Metalio sleeps powered off and only its button turns it back on.
  return isXteinkDevice() || BoardConfig::isPaperMono() || BoardConfig::isSticky() || BoardConfig::isMetalioEInk4();
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();
  const bool usbConnected = isUsbConnected();
  return static_cast<WakeupReason>(xp_hal_wake_reason(
      resetReason == ESP_RST_DEEPSLEEP, wakeupCause == ESP_SLEEP_WAKEUP_GPIO || wakeupCause == ESP_SLEEP_WAKEUP_EXT1,
      wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED, resetReason == ESP_RST_POWERON, resetReason == ESP_RST_UNKNOWN,
      usbConnected, coldBootImpliesPowerButton()));
}
