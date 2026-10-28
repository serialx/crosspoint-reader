#include <BoardConfig.h>

#include "src/driver/Uc8253X3Driver.h"
#if defined(FREEINK_RUST_PANEL) && FREEINK_RUST_PANEL
#include "src/driver/RustPanelDriver.h"
#endif
#include <array>
#include <iostream>
#include <vector>

#include "Trace.h"

static int variant;
namespace freeink {
const Uc8253X3Config& traceX3Config() {
  static Uc8253X3Config cfg = uc8253X3DefaultConfig();
  static std::array<uint8_t, 42> modified;
  if (variant == 1) cfg.directGray = nullptr;
  if (variant == 2) {
    for (unsigned i = 0; i < modified.size(); ++i) modified[i] = static_cast<uint8_t>(i * 31 + 7);
    cfg.fast.ww = modified.data();
    cfg.gc.bw = modified.data();
    cfg.lutLen = 17;
  }
  return cfg;
}
}  // namespace freeink
using namespace freeink;
int main(int argc, char** argv) {
  variant = argc > 1 ? std::atoi(argv[1]) : 0;
  BoardConfig::ACTIVE = BoardConfig::XTEINK_X3;
  trace::configure(false, 2, {});
#if defined(FREEINK_RUST_PANEL) && FREEINK_RUST_PANEL
  RustPanelDriver panel(XP_DISPLAY_UC8253_X3);
#else
  Uc8253X3Driver panel(traceX3Config());
#endif
  EpdBus bus;
  const auto& pins = BoardConfig::ACTIVE.display;
  bus.begin({pins.sclk, pins.mosi, pins.cs, pins.dc, pins.rst, pins.busy, pins.powerEnable}, panel.spiHz(),
            panel.busyPolarity());
  std::vector<uint8_t> a(52272), b(52272), lsb(52272), msb(52272);
  for (unsigned i = 0; i < a.size(); ++i) {
    a[i] = (i * 37) ^ (i >> 8);
    b[i] = ~a[i];
    lsb[i] = (i * 17) ^ (i >> 7);
    msb[i] = (i * 29) ^ (i >> 4);
  }
  trace::step("begin");
  panel.begin(bus);
  trace::step("unmatched finish");
  panel.displayFinish(bus, nullptr);
  trace::step("skip initial");
  panel.skipInitialResync();
  for (auto mode : {RefreshMode::Full, RefreshMode::Half, RefreshMode::Fast}) {
    for (bool off : {false, true}) {
      trace::step("display mode=" + std::to_string(static_cast<int>(mode)) + " off=" + std::to_string(off));
      panel.display(bus, a.data(), b.data(), mode, off);
      trace::step("async");
      trace::event("pending " + std::to_string(panel.displayStart(bus, b.data(), a.data(), mode, off)));
      panel.displayFinish(bus, b.data());
    }
  }
  for (uint8_t passes : {0, 1, 3}) {
    trace::step("resync " + std::to_string(passes));
    panel.requestResync(passes);
    panel.display(bus, a.data(), nullptr, RefreshMode::Fast, false);
  }
  for (auto mode : {GrayscaleMode::Overlay, GrayscaleMode::Absolute, GrayscaleMode::Direct}) {
    trace::step("gray mode=" + std::to_string(static_cast<int>(mode)));
    const auto c = panel.grayscaleCapabilities(mode);
    trace::event("caps " + std::to_string(static_cast<int>(c.encoding)) + " " +
                 std::to_string(static_cast<int>(c.base)) + " " + std::to_string(c.stripUploads));
    panel.beginGrayscale(bus, a.data(), mode, RefreshMode::Half, false);
    panel.preconditionGrayscale(bus, 13, 17, 91, 33);
    panel.preconditionGrayscale(bus, 790, 526, 65535, 65535);
    panel.preconditionGrayscale(bus, 800, 600, 1, 1);
    panel.preconditionGrayscale(bus, 0, 0, 0, 5);
    trace::step("missing planes");
    panel.copyGrayscaleLsb(bus, nullptr);
    panel.copyGrayscaleMsb(bus, msb.data());
    panel.displayGray(bus, a.data(), false, nullptr, false);
    trace::step("whole gray");
    panel.copyGrayscaleLsb(bus, lsb.data());
    panel.copyGrayscaleMsb(bus, msb.data());
    panel.displayGray(bus, a.data(), true, nullptr, false);
    trace::step("cleanup null");
    panel.cleanupGrayscaleBuffers(bus, nullptr);
    trace::step("gray to bw");
    panel.display(bus, b.data(), nullptr, RefreshMode::Fast, false);
    trace::step("gray strips");
    panel.beginGrayscale(bus, a.data(), mode, RefreshMode::Fast, false);
    panel.writeGrayscalePlaneStrip(bus, GrayPlane::Lsb, nullptr, 0, 0);
    panel.writeGrayscalePlaneStrip(bus, GrayPlane::Lsb, lsb.data(), 0, 0);
    for (auto plane : {GrayPlane::Lsb, GrayPlane::Msb})
      for (uint16_t y = 0; y < 528; y += 19)
        panel.writeGrayscalePlaneStrip(bus, plane, (plane == GrayPlane::Lsb ? lsb : msb).data() + y * 99, y,
                                       std::min<unsigned>(19, 528 - y));
    panel.displayGray(bus, a.data(), false, nullptr, false);
    panel.cleanupGrayscaleBuffers(bus, a.data());
    trace::step("factory");
    panel.beginGrayscale(bus, a.data(), mode, RefreshMode::Full, true);
    panel.copyGrayscaleLsb(bus, lsb.data());
    panel.copyGrayscaleMsb(bus, msb.data());
    panel.displayGrayCalibration(bus, a.data(), 0, 0, 792, 528);
    trace::step("revert");
    panel.grayscaleRevert(bus, a.data());
    trace::step("sleep wake");
    panel.deepSleep(bus);
    panel.begin(bus);
    panel.display(bus, a.data(), nullptr, RefreshMode::Fast, true);
  }
  trace::step("no-op hooks");
  panel.seedPreviousFrame(bus, a.data());
  panel.prepareGrayscaleTarget(a.data());
  panel.beginDisplayWork();
  panel.abortPostRefresh();
  panel.runMaintenance(bus);
  panel.controllerIdle(bus);
  panel.setCustomLut(bus, true, nullptr);
  panel.setBackgroundHint(true);
  trace::event("status " + std::to_string(panel.displayCommitted()) + " " + std::to_string(panel.postRefreshAborted()) +
               " " + std::to_string(panel.hasPendingMaintenance()));
  trace::finish();
}
