#include <BoardConfig.h>

#include <algorithm>
#include <vector>

#include "Trace.h"
#include "src/driver/Uc8179Driver.h"
#include "src/driver/Uc8279Driver.h"
#include "src/driver/Uc8279X4Driver.h"
#include "src/driver/Uc8279cA4Driver.h"

int ultraChipTrace(const std::string& controller, int failAfter) {
  using namespace freeink;
  PanelDriver* panel = nullptr;
#if FREEINK_DRIVER_UC8179
  if (controller == "uc8179") panel = &uc8179Driver();
#endif
#if FREEINK_DRIVER_UC8279
  if (controller == "uc8279-x3") panel = &uc8279Driver();
#endif
#if FREEINK_DRIVER_UC8279_X4
  if (controller.starts_with("uc8279-") && controller != "uc8279-x3") panel = &uc8279X4Driver();
#endif
#if FREEINK_DRIVER_UC8279C
  if (controller == "uc8279c") panel = &uc8279cA4Driver();
#endif
  if (!panel) return 2;
  const auto geometry = panel->geometry();
  std::vector<uint8_t> bw(geometry.bufferSize), lsb(bw.size()), msb(bw.size());
  for (size_t i = 0; i < bw.size(); ++i) {
    bw[i] = static_cast<uint8_t>(i * 37 ^ i >> 7 ^ 0xa5);
    lsb[i] = static_cast<uint8_t>(i * 19 ^ i >> 11 ^ 0x33);
    msb[i] = static_cast<uint8_t>(i * 31 ^ i >> 3 ^ 0x77);
  }
  EpdBus bus;
  const auto& pins = BoardConfig::ACTIVE.display;
  bus.begin({pins.sclk, pins.mosi, pins.cs, pins.dc, pins.rst, pins.busy, pins.powerEnable}, panel->spiHz(),
            panel->busyPolarity());
  trace::allocationTrace(failAfter);
  trace::step("raw begin");
  panel->begin(bus);
  panel->skipInitialResync();
  panel->beginDisplayWork();
  panel->abortPostRefresh();
  panel->setBackgroundHint(true);
  panel->requestCompleteWaveformNextRefresh();
  panel->setFullRefreshCompletesWaveform(true);
  panel->setFastRefreshCutoffMs(1);
  panel->setHoldPeriodicFull(true);
  for (auto mode : {RefreshMode::Full, RefreshMode::Half, RefreshMode::Fast}) {
    trace::step("raw display " + std::to_string(static_cast<int>(mode)));
    panel->display(bus, bw.data(), nullptr, mode, true);
    for (auto& b : bw) b ^= 0xff;
    trace::event("async " + std::to_string(panel->displayStart(bus, bw.data(), nullptr, mode, true)));
    panel->displayFinish(bus, bw.data());
    panel->displayFinish(bus, bw.data());
  }
  for (auto mode : {GrayscaleMode::Overlay, GrayscaleMode::Absolute, GrayscaleMode::Direct}) {
    for (auto fallback : {RefreshMode::Fast, RefreshMode::Half, RefreshMode::Full}) {
      trace::step("raw gray " + std::to_string(static_cast<int>(mode)) + " " +
                  std::to_string(static_cast<int>(fallback)));
      panel->beginGrayscale(bus, bw.data(), mode, fallback, true);
      panel->copyGrayscaleLsb(bus, nullptr);
      panel->copyGrayscaleMsb(bus, nullptr);
      panel->copyGrayscaleMsb(bus, msb.data());
      panel->displayGray(bus, msb.data(), false, nullptr, mode != GrayscaleMode::Overlay);
      panel->copyGrayscaleLsb(bus, lsb.data());
      panel->copyGrayscaleMsb(bus, msb.data());
      panel->displayGray(bus, msb.data(), true, nullptr, mode != GrayscaleMode::Overlay);
      panel->cleanupGrayscaleBuffers(bus, nullptr);
      panel->requestResync(3);
      panel->display(bus, bw.data(), nullptr, RefreshMode::Fast, false);
      panel->grayscaleRevert(bus, bw.data());
      panel->preconditionGrayscale(bus, 13, 17, 65535, 65535);
      panel->preconditionGrayscale(bus, 0, 0, 0, 1);
    }
  }
  trace::step("raw strip errors and recovery");
  panel->writeGrayscalePlaneStrip(bus, GrayPlane::Lsb, nullptr, 0, 1);
  panel->writeGrayscalePlaneStrip(bus, GrayPlane::Lsb, lsb.data(), 0, 0);
  panel->writeGrayscalePlaneStrip(bus, GrayPlane::Lsb, lsb.data(), geometry.height, 1);
  panel->writeGrayscalePlaneStrip(bus, GrayPlane::Lsb, lsb.data(), geometry.height - 1, 2);
  trace::allocationRecovery();
  panel->begin(bus);
  panel->beginGrayscale(bus, bw.data(), GrayscaleMode::Overlay, RefreshMode::Half, false);
  for (uint16_t y = 0; y < geometry.height; y += 19) {
    auto rows = std::min<unsigned>(19, geometry.height - y);
    panel->writeGrayscalePlaneStrip(bus, GrayPlane::Lsb, lsb.data() + y * geometry.widthBytes, y, rows);
    panel->writeGrayscalePlaneStrip(bus, GrayPlane::Msb, msb.data() + y * geometry.widthBytes, y, rows);
  }
  panel->prepareGrayscaleTarget(bw.data());
  panel->displayGray(bus, msb.data(), true, nullptr, false);
  panel->cleanupGrayscaleBuffers(bus, bw.data());
  trace::step("raw sparse overlay");
  std::fill(lsb.begin(), lsb.end(), 0);
  std::fill(msb.begin(), msb.end(), 0);
  for (size_t i = 0; i < msb.size(); i += 97) msb[i] = 0x30;
  panel->beginGrayscale(bus, bw.data(), GrayscaleMode::Overlay, RefreshMode::Fast, false);
  panel->copyGrayscaleLsb(bus, lsb.data());
  panel->copyGrayscaleMsb(bus, msb.data());
  panel->displayGray(bus, msb.data(), false, nullptr, false);
  panel->display(bus, bw.data(), nullptr, RefreshMode::Fast, true);
  panel->displayGrayCalibration(bus, msb.data(), 0, 0, geometry.width, geometry.height);
  panel->runMaintenance(bus);
  panel->controllerIdle(bus);
  trace::event("state aborted=" + std::to_string(panel->postRefreshAborted()) +
               " committed=" + std::to_string(panel->displayCommitted()) +
               " maintenance=" + std::to_string(panel->hasPendingMaintenance()));
  panel->deepSleep(bus);
  trace::finish();
  return 0;
}
