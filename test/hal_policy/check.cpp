#include <Arduino.h>
#include <BoardConfig.h>
#include <DeviceSecret.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalHaptics.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <Preferences.h>
#include <SDCardManager.h>
#include <TrustedTime.h>
#include <UsbMassStorage.h>
#include <WiFi.h>
#include <XteinkDetect.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <nvs.h>

#include <cstring>
#include <initializer_list>
#include <new>
static size_t allocations = 0, allocationBytes = 0, releases = 0;
void* operator new(size_t size) {
  ++allocations;
  allocationBytes += size;
  if (void* p = malloc(size)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
  if (p) ++releases;
  free(p);
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
extern char panicMessage[256];
extern HalSystem::StackFrame panicStack[32];
extern volatile uint32_t panicCaptureMarker;
extern "C" void __real_panic_abort(const char*) {}
extern "C" void __real_panic_print_backtrace(const void*, int) {}
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  const int variant = atoi(argv[2]);
  if (!strcmp(argv[1], "secret")) {
    if (variant == 1) openError = 2;
    if (variant >= 2) readError = 1;
    if (variant == 3) writeError = 2;
    if (variant == 4) commitError = 2;
    if (variant == 5) readError = 2;
    if (variant == 6) {
      readError = 0;
      secretSize = 31;
    }
    uint8_t out[32];
    memset(out, 0x19, sizeof(out));
    for (int i = 0; i < 3; ++i) {
      const bool ok = deviceSecret(out);
      printf("result %u ", ok);
      for (auto v : out) printf("%02x", v);
      puts("");
      openError = readError = writeError = commitError = 0;
      secretSize = 32;
    }
  } else if (!strcmp(argv[1], "time")) {
    constexpr int64_t base = 1735689600;
    storedFloor = variant == 1 ? base : variant == 2 ? 4102444801LL : 0;
    clockNow = variant == 3 ? base + 86400 : 0;
    prefsOk = variant != 4;
    trustedtime::init();
    for (int64_t value : {int64_t(0), base - 1, base, base + 59, base + 60, base + 31536001, int64_t(4102444800LL),
                          int64_t(4102444801LL)}) {
      trustedtime::adopt(value);
      printf("now %lld\n", (long long)trustedtime::trustedNow());
    }
    clockNow = base - 1;
    trustedtime::note();
    clockNow = base + 90;
    trustedtime::note();
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    trustedtime::startSync();
    trustedtime::startSync();
    syncAfter = 10;
    syncPolls = 0;
    printf("sync %u\n", trustedtime::syncNow(200));
    syncAfter = 0;
    printf("sync %u\n", trustedtime::syncNow(200));
  } else if (!strcmp(argv[1], "light")) {
    lightPresent = variant != 1;
    Frontlight.begin(255, 210, variant != 2);
    for (auto v : {0, 50, 100, 101, 255}) {
      Frontlight.setBrightness(v);
      Frontlight.setWarmth(v);
      Frontlight.setOn(false);
      Frontlight.setOn(false);
      Frontlight.setOn(true);
      printf("light state %u %u %u\n", Frontlight.brightness(), Frontlight.warmth(), Frontlight.isOn());
    }
  } else if (!strcmp(argv[1], "tilt")) {
    imuBegin = variant != 1;
    imuSleep = variant != 2;
    imuGyro = variant != 3;
    imuWake = variant != 4;
    HalTiltSensor sensor;
    sensor.begin();
    for (uint8_t mode : {1, 2, 0})
      for (uint8_t orientation : {0, 1, 2, 3, 4}) {
        for (float v : {0.f, 270.f, 271.f, 49.f, -270.f, -271.f, 0.f}) {
          sampleX = v;
          sampleY = -v;
          for (uint32_t t : {0u, 49u, 50u, 299u, 300u, 599u, 600u}) {
            ticks += t;
            sensor.update(mode, orientation, true);
            printf("events %u %u %u\n", sensor.wasTiltedForward(), sensor.wasTiltedBack(), sensor.hadActivity());
          }
        }
      }
    imuRead = false;
    ticks += 1000;
    sensor.update(1, 0, true);
    sensor.update(1, 0, false);
    sensor.clearPendingEvents();
    sensor.deepSleep();
  } else if (!strcmp(argv[1], "haptics")) {
    motorBegin = variant == 0;
    for (bool enabled : {false, true})
      for (bool pressed : {false, true})
        for (uint8_t intensity : {0, 1, 2, 255}) {
          HalHaptics::feedback(enabled, pressed, intensity);
          HalHaptics::longPress(enabled, intensity);
        }
  } else if (!strcmp(argv[1], "system")) {
    strcpy(panicMessage, "panic fixture");
    panicStack[0].sp = 0x12345678;
    for (unsigned i = 0; i < 8; ++i) panicStack[0].spp[i] = i;
    resetCause = variant == 0   ? ESP_RST_POWERON
                 : variant == 1 ? ESP_RST_PANIC
                 : variant == 2 ? ESP_RST_CPU_LOCKUP
                                : ESP_RST_TASK_WDT;
    panicCaptureMarker = variant == 3 ? 0 : 0x50414e49u;
    fileOk = variant != 4;
    writeLength = variant == 5 ? 3 : -2;
    HalSystem::begin();
    printf("panic %u\n", HalSystem::isRebootFromPanic());
    puts(HalSystem::getPanicInfo(true).c_str());
    HalSystem::checkPanic();
    printf("marker %u\n", panicCaptureMarker);
    HalSystem::clearPanic();
  } else if (!strcmp(argv[1], "storage")) {
    fileOk = variant != 1;
    directory = variant == 2;
    fileLength = variant == 3 ? 0 : 40;
    largestBlock = variant == 5 ? 551 : 552;
    readLength = variant == 6 ? 39 : variant == 7 ? -1 : -2;
    std::string result = "old";
    printf("string result %u\n", Storage.readFileToString("TEST", "/book", variant == 4 ? 39 : 40, result));
    printf("bytes %zu ", result.size());
    for (auto b : result) printf("%02x", uint8_t(b));
    puts("");
    {
      HalFile file;
      Storage.openFileForRead("TEST", "/old", file);
      Storage.openFileForRead("TEST", "/new", file);
    }
    detachOk = variant != 8;
    usbBegin = variant != 9;
    mountOk = false;
    printf("usb result %u\n", Storage.beginUsbDrive());
  } else if (!strcmp(argv[1], "display")) {
    deviceOverride = variant % 2 + 1;
    gpio.begin();
    for (int reset : {ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_DEEPSLEEP, ESP_RST_OTHER}) {
      resetCause = reset;
      wakeCause = ESP_SLEEP_WAKEUP_GPIO;
      digitalValue = 1;
      charging = true;
      display.begin(variant >= 2);
    }
    for (int mode : {0, 1, 2, 3})
      for (bool off : {false, true}) {
        auto refresh = static_cast<HalDisplay::RefreshMode>(mode);
        display.displayBuffer(refresh, off);
        display.displayBufferAsync(refresh);
        display.refreshDisplay(refresh, off);
        display.displayGrayscaleBase(refresh, off);
        display.displayGrayscaleBase(HalDisplay::GrayscaleMode::Overlay, refresh, off);
      }
  } else if (!strcmp(argv[1], "gpio")) {
    deviceOverride = variant < 4 ? variant : 0;
    deviceCached = variant >= 4 ? variant - 4 : 0;
    freeink::verdict = variant % 2 ? freeink::XteinkVerdict::X3Confirmed : freeink::XteinkVerdict::X4Confirmed;
    gpio.begin();
    for (int reset : {ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_DEEPSLEEP, ESP_RST_OTHER})
      for (int wake :
           {ESP_SLEEP_WAKEUP_UNDEFINED, ESP_SLEEP_WAKEUP_GPIO, ESP_SLEEP_WAKEUP_EXT1, ESP_SLEEP_WAKEUP_TIMER})
        for (bool usb : {false, true}) {
          resetCause = reset;
          wakeCause = wake;
          digitalValue = usb;
          charging = usb;
          printf("reason %u\n", unsigned(gpio.getWakeupReason()));
        }
    for (int adc : {-1, 0, 3999, 4000, 4095}) {
      adc1 = adc;
      printf("raw %u\n", gpio.rawInputActive());
    }
    for (uint8_t mask : {0, 4, 12, 8, 0}) {
      pageMask = mask;
      digitalValue = mask != 0;
      gpio.update();
      printf("edges %u %u\n", gpio.wasCapacitivePagePressed(), gpio.wasUsbStateChanged());
    }
    physical = true;
    printf("verify %u\n", gpio.verifyPowerButtonWakeup());
    physical = false;
    printf("verify %u\n", gpio.verifyPowerButtonWakeup());
  } else if (!strcmp(argv[1], "power")) {
    wifiConnected = false;
    chargerOk = variant == 1;
    cpuFreq = variant == 2 ? 0 : 160;
    powerManager.setPowerSaving(true);
    powerManager.begin();
    for (bool wifi : {false, true, false})
      for (bool ok : {false, true}) {
        wifiConnected = wifi;
        cpuOk = ok;
        powerManager.setPowerSaving(true);
        {
          HalPowerManager::Lock first;
          HalPowerManager::Lock nested;
          powerManager.setPowerSaving(true);
        }
        powerManager.setPowerSaving(true);
        powerManager.setPowerSaving(false);
      }
    BoardConfig::ACTIVE.batteryGauge.gaugeAddr = variant == 3 ? 0 : 1;
    for (uint32_t t : {0u, 1u, 1499u, 1500u, 1501u, 3000u, 0xfffffff0u, 30u, 1500u}) {
      ticks = t;
      for (bool ok : {true, false}) {
        batteryOk = ok;
        batteryPercent = (t % 101);
        printf("percent %u\n", powerManager.getBatteryPercentage());
      }
    }
  } else if (!strcmp(argv[1], "clock")) {
    rtcBegin = variant != 1;
    rtcRead = variant != 2;
    wifiConnected = variant != 3;
    rtcWrite = variant != 4;
    HalClock clock;
    clock.begin();
    clock.setTimezone("UTC0");
    for (uint32_t t : {0u, 1u, 9999u, 10000u, 20000u, 0xfffffff0u, 40u}) {
      ticks = t;
      for (bool twelve : {false, true})
        for (size_t size : {0u, 5u, 6u, 8u, 9u, 20u}) {
          char buf[20] = "unchanged";
          const bool ok = clock.formatTime(buf, size, twelve);
          printf("clock %u %s\n", ok, buf);
        }
      rtcRead = !rtcRead;
    }
    clockNow = 1793279737;
    syncAfter = variant == 5 ? 1000 : 2;
    clock.syncFromNTP();
  }
  printf("allocations %zu bytes %zu releases %zu\n", allocations, allocationBytes, releases);
}
