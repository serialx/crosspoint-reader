#include <BoardConfig.h>
#include <esp_heap_caps.h>

#include <array>
#include <cstdlib>
#include <string>

#include "Trace.h"
#if FREEINK_DEVICE_PAPERMONO
#include "src/driver/PaperMonoDriver.h"
#else
#include "src/driver/Ssd2677Driver.h"
#endif

// Fresh process per failure index: driver singletons retain allocations.
int main(int argc, char** argv) {
  const int failure = argc > 1 ? std::atoi(argv[1]) : 0;
  displayTraceAllocations = true;
  displayTraceDiagnostics = true;
  displayFailAllocation = failure;
#if FREEINK_DEVICE_PAPERMONO
  auto& driver = freeink::paperMonoDriver();
  trace::configure(false, 0, {});
#else
  auto& driver = freeink::ssd2677Driver();
  trace::configure(false, 1, {});
#endif
  freeink::EpdBus bus;
  const auto& pins = BoardConfig::ACTIVE.display;
  bus.begin({pins.sclk, pins.mosi, pins.cs, pins.dc, pins.rst, pins.busy, pins.powerEnable}, driver.spiHz(),
            driver.busyPolarity());
  static std::array<uint8_t, 48000> bw, lsb, msb;
  for (size_t i = 0; i < bw.size(); ++i) {
    bw[i] = static_cast<uint8_t>((i * 37) ^ (i >> 8) ^ 0xA5);
    lsb[i] = static_cast<uint8_t>(i * 13 + (i >> 7));
    msb[i] = static_cast<uint8_t>(i * 23 + (i >> 9));
  }
  const auto status = [&] {
    trace::event("state committed=" + std::to_string(driver.displayCommitted()) +
                 " aborted=" + std::to_string(driver.postRefreshAborted()) +
                 " maintenance=" + std::to_string(driver.hasPendingMaintenance()));
  };
  trace::step("begin-failure-" + std::to_string(failure));
  driver.begin(bus);
  driver.beginDisplayWork();
  driver.display(bus, bw.data(), nullptr, freeink::RefreshMode::Fast, false);
  status();
  trace::step("recover");
  driver.beginDisplayWork();
  driver.display(bus, bw.data(), nullptr, freeink::RefreshMode::Full, false);
  status();
  trace::step("gray-without-planes");
  driver.beginDisplayWork();
  driver.beginGrayscale(bus, bw.data(), freeink::GrayscaleMode::Overlay, freeink::RefreshMode::Half, false);
  driver.displayGray(bus, bw.data(), false, nullptr, false);
  status();
  for (const bool cancel : {false, true}) {
    trace::step(cancel ? "cancel-gray" : "partial-gray");
    driver.beginDisplayWork();
    driver.displayGrayscaleBase(bus, bw.data(), freeink::RefreshMode::Half, false);
    driver.copyGrayscaleLsb(bus, lsb.data());
    driver.writeGrayscalePlaneStrip(bus, freeink::GrayPlane::Msb, msb.data(), 0, 479);
    if (cancel) driver.abortPostRefresh();
    driver.displayGray(bus, bw.data(), false, nullptr, false);
    driver.cleanupGrayscaleBuffers(bus, bw.data());
    status();
  }
  trace::step("complete-gray");
  driver.beginDisplayWork();
  driver.displayGrayscaleBase(bus, bw.data(), freeink::RefreshMode::Full, false);
  driver.copyGrayscaleLsb(bus, lsb.data());
  driver.copyGrayscaleMsb(bus, msb.data());
  driver.displayGray(bus, bw.data(), false, nullptr, false);
  status();
  trace::step("stale-gray-new-work");
  driver.copyGrayscaleLsb(bus, lsb.data());
  driver.beginDisplayWork();
  driver.copyGrayscaleMsb(bus, msb.data());
  driver.displayGray(bus, bw.data(), false, nullptr, false);
  status();
  trace::step("window-and-idle");
  driver.requestResync(1);
  driver.beginDisplayWork();
  driver.display(bus, bw.data(), nullptr, freeink::RefreshMode::Full, false);
  bw[17 * 100 + 2] ^= 0x55;
  driver.displayWindow(bus, bw.data(), nullptr, 16, 17, 88, 33, true);
  driver.displayWindow(bus, bw.data(), nullptr, 13, 17, 91, 33, false);
  driver.controllerIdle(bus);
  status();
#if FREEINK_DEVICE_PAPERMONO
  for (const uint8_t frames : {0, 1, 6, 11, 12, 255}) {
    trace::step("gray-params-" + std::to_string(frames));
    freeink::PaperMonoGrayParams params;
    params.lightFrames = frames;
    params.cleanupPasses = 3;
    driver.setGrayParams(params);
    driver.beginDisplayWork();
    driver.displayGrayscaleBase(bus, bw.data(), freeink::RefreshMode::Full, false);
    driver.copyGrayscaleLsb(bus, lsb.data());
    driver.copyGrayscaleMsb(bus, msb.data());
    driver.displayGray(bus, bw.data(), false, nullptr, false);
    status();
  }
#endif
  trace::step("sleep-wake");
  driver.deepSleep(bus);
  driver.begin(bus);
  status();
  trace::finish();
}
