#include <BoardConfig.h>

#include <array>
#include <cstdlib>
#include <string>
#include <vector>

#include "Trace.h"
#include "src/driver/Ssd1677Driver.h"
#ifdef DISPLAY_TRACE_RUST_BACKEND
#include "src/driver/RustPanelDriver.h"
#endif

static unsigned variant;
namespace freeink {
const Ssd1677Config& traceSsdConfig() {
  static Ssd1677Config c = ssd1677DefaultConfig();
  static std::array<unsigned char, 110> lut;
  for (unsigned i = 0; i < lut.size(); ++i) lut[i] = static_cast<uint8_t>(i * 13 + 7);
  switch (variant) {
    case 1:
      for (unsigned i = 0; i < 5; ++i) c.booster[i] = i + 1;
      break;
    case 2:
      c.driverOutputScan = 4;
      break;
    case 3:
      c.borderWaveformInit = 3;
      break;
    case 4:
      c.halfRefreshTemp = 0x71;
      break;
    case 5:
      c.grayLut = lut.data();
      break;
    case 6:
      c.fullSeqOverride = 0;
      c.fastSeqOverride = 0;
      c.halfSeqOverride = 0;
      break;
    case 7:
      c.fullSeqOverride = 0xf4;
      c.fastSeqOverride = 0xdc;
      c.halfSeqOverride = 0;
      break;
    case 8:
      c.borderWaveformFull = 1;
      c.borderWaveformFast = 2;
      c.borderWaveformHalf = 0;
      break;
    case 9:
      c.borderWaveformFull = 0;
      c.borderWaveformFast = 0;
      c.borderWaveformHalf = 3;
      break;
    case 10:
      c.borderWaveformGray = 0;
      break;
    case 11:
      c.grayPowerUpFirst = true;
      break;
    case 12:
      c.absoluteGrayscale = false;
      break;
    case 13:
      c.overlayGrayscale = false;
      break;
    case 14:
      c.writeSecondUpdateControlByte = true;
      break;
    case 15:
      c.restoreInternalTemperature = true;
      break;
    case 16:
      c.blackPulseClean = true;
      break;
    case 17:
      c.factoryGrayLut = lut.data();
      break;
    case 18:
      c.fastLut = lut.data();
      break;
    case 19:
      c.fullLut = lut.data();
      break;
    case 20:
      c.halfLut = lut.data();
      break;
    case 21:
      c.grayLut = nullptr;
      c.overlayGrayscale = false;
      c.absoluteGrayscale = false;
      break;
    default:
      break;
  }
  return c;
}
}  // namespace freeink

int main(int argc, char** argv) {
  using namespace freeink;
  variant = argc > 1 ? std::atoi(argv[1]) : 0;
  BoardConfig::ACTIVE = BoardConfig::XTEINK_X4;
  BoardConfig::ACTIVE.orientation.mirrorX = variant == 22;
  BoardConfig::ACTIVE.orientation.mirrorY = variant == 23;
  trace::configure(false, 0, {});
  displayTraceDiagnostics = true;
#ifdef DISPLAY_TRACE_RUST_BACKEND
  alignas(void*) uint8_t state[XP_DISPLAY_SSD1677_STATE_BYTES]{};
  RustPanelDriver panel(XP_DISPLAY_SSD1677, state, sizeof(state), 0, nullptr, 0);
#else
  Ssd1677Driver panel(traceSsdConfig());
#endif
  EpdBus bus;
  const auto& p = BoardConfig::ACTIVE.display;
  bus.begin({p.sclk, p.mosi, p.cs, p.dc, p.rst, p.busy, p.powerEnable}, panel.spiHz(), panel.busyPolarity());
  std::vector<uint8_t> frame(48000, 0xa5), previous(48000, 0x5a);
  trace::step("begin");
  panel.begin(bus);
  for (auto mode : {RefreshMode::Full, RefreshMode::Half, RefreshMode::Fast}) {
    for (bool off : {false, true}) {
      trace::step("display " + std::to_string(static_cast<int>(mode)) + " off=" + std::to_string(off));
      panel.display(bus, frame.data(), previous.data(), mode, off);
      trace::step("async");
      panel.displayStart(bus, frame.data(), previous.data(), mode, off);
      panel.displayFinish(bus, frame.data());
    }
  }
  for (auto mode : {GrayscaleMode::Overlay, GrayscaleMode::Absolute, GrayscaleMode::Direct}) {
    const auto cap = panel.grayscaleCapabilities(mode);
    trace::event("caps " + std::to_string(static_cast<int>(cap.encoding)) + " " +
                 std::to_string(static_cast<int>(cap.base)) + " " + std::to_string(cap.stripUploads));
    panel.beginGrayscale(bus, frame.data(), mode, RefreshMode::Half, false);
    panel.copyGrayscaleLsb(bus, frame.data());
    panel.copyGrayscaleMsb(bus, previous.data());
    panel.displayGray(bus, frame.data(), false, nullptr, false);
    panel.cleanupGrayscaleBuffers(bus, frame.data());
    panel.displayGray(bus, frame.data(), false, nullptr, true);
    panel.display(bus, frame.data(), nullptr, RefreshMode::Half, false);
  }
  trace::step("gray-calibration");
  panel.displayGrayCalibration(bus, frame.data(), 16, 24, 32, 40);
  trace::step("sleep");
  panel.deepSleep(bus);
  trace::finish();
}
