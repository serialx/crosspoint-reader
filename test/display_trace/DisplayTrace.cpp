#include <FreeInkDisplay.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "Trace.h"

using freeink::FreeInkDisplay;
int ultraChipTrace(const std::string& controller, int failAfter);

static uint8_t grayscaleByte(uint32_t index, uint32_t widthBytes, uint32_t seed) {
  const uint32_t row = index / widthBytes;
  const uint32_t column = index % widthBytes;
  uint32_t value = seed ^ (index * 0x9e3779b9u) ^ (row * 0x85ebca6bu) ^ (column * 0xc2b2ae35u);
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return static_cast<uint8_t>(value);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: runner CONTROLLER SCRIPT [--full] [PAGE.raw]\n";
    return 2;
  }
  const std::string controller = argv[1];
  std::vector<uint8_t> probe;
  int polarity = 0;
  if (controller == "uc8253") {
    BoardConfig::ACTIVE = BoardConfig::XTEINK_X3;
    polarity = 2;
  } else if (controller == "uc8279-x3") {
    BoardConfig::ACTIVE = BoardConfig::XTEINK_X3_UC8279;
    polarity = 2;
  } else if (controller == "uc8179") {
    BoardConfig::ACTIVE.displayController = BoardConfig::DisplayController::UC8179;
    polarity = 3;
  } else if (controller.starts_with("uc8279-")) {
    BoardConfig::ACTIVE.displayController = BoardConfig::DisplayController::UC8279;
    BoardConfig::ACTIVE.displayControllerVariant = static_cast<uint8_t>(std::stoul(controller.substr(7), nullptr, 16));
    polarity = 3;
  } else if (controller == "ssd2677") {
    probe = {0x07};
    polarity = 1;
#if FREEINK_DEVICE_PICCO
    probe = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 1, 1};
#endif
  } else if (controller == "yrd") {
    probe = {0xca, 0xfe, 0, 0x16, 0x77, 2, 0x10, 1, 0, 0x91};
  } else if (controller == "depg") {
    probe = {0xca, 0xfe, 0, 0x16, 0x77, 0, 0, 0, 0, 0};
  } else if (controller == "opm") {
    probe = {'W', '0', '4', '0', 'B', '3', 0, 0, 0, 0};
  } else if (controller == "uc8279c") {
    polarity = 1;
  }
  trace::configure(argc > 3 && std::string(argv[3]) == "--full", polarity, probe);
  trace::event("display-trace-v1 controller=" + controller);
  if (std::string(argv[2]) == "--ultrachip") {
    return ultraChipTrace(controller, argc > 4 ? std::stoi(argv[4]) : -1);
  }
  FreeInkDisplay display(-1, -1, -1, -1, -1, -1);
  if (controller == "uc8253" || controller == "uc8279-x3") display.setDisplayX3();
  std::vector<uint8_t> bw, lsb, msb;
  std::ifstream script(argv[2]);
  if (!script) return 2;
  std::string line;
  while (std::getline(script, line)) {
    if (line.empty() || line[0] == '#') continue;
    trace::step(line);
    std::istringstream args(line);
    std::string op;
    args >> op;
    int a = 0;
    args >> a;
    if (op == "bus-probe") {
      freeink::EpdBus bus;
      const auto& d = BoardConfig::ACTIVE.display;
      bus.begin({d.sclk, d.mosi, d.cs, d.dc, d.rst, d.busy, d.powerEnable}, 10000000,
                static_cast<freeink::BusyPolarity>(a));
      for (const auto duration : {4000ULL, 40000ULL, 0ULL, 60000000ULL}) {
        if (a == 3 && duration == 60000000ULL) continue;
        trace::event("busy.duration_us " + std::to_string(duration));
        trace::busyDuration(duration);
        bus.waitBusy("probe");
        bus.waitRefreshComplete("probe");
      }
      trace::busyDuration(40000);
      bus.setBusyWaitHooks([] { trace::event("hook.begin"); }, [] { trace::event("hook.end"); });
      bus.setBusyWaitSliceHook([](int8_t pin, uint8_t level) {
        trace::event("hook.slice " + std::to_string(pin) + " " + std::to_string(level));
        return false;
      });
      bus.waitRefreshComplete("hooked");
      trace::busyDuration(4000);
    } else if (op == "begin" || op == "wake") {
      display.begin();
      const auto n = display.getBufferSize();
      bw.resize(n);
      lsb.resize(n);
      msb.resize(n);
      for (size_t i = 0; i < n; ++i) {
        bw[i] = static_cast<uint8_t>((i * 37) ^ (i >> 8) ^ 0xa5);
        lsb[i] = grayscaleByte(i, display.getDisplayWidthBytes(), 0x13579bdfu);
        msb[i] = grayscaleByte(i, display.getDisplayWidthBytes(), 0x2468ace0u);
      }
      trace::event("geometry " + std::to_string(display.getDisplayWidth()) + " " +
                   std::to_string(display.getDisplayHeight()));
    } else if (op == "frame") {
      if (a == 1)
        for (auto& b : bw) b ^= 0xff;
      display.setFramebuffer(bw.data());
    } else if (op == "page") {
      if (argc < 5) {
        trace::event("page.external absent");
        continue;
      }
      std::ifstream page(argv[4], std::ios::binary);
      page.read(reinterpret_cast<char*>(bw.data()), bw.size());
      if (page.gcount() != static_cast<std::streamsize>(bw.size()) || page.peek() != EOF) return 3;
      display.setFramebuffer(bw.data());
    } else if (op == "clear") {
      display.clearScreen(a);
    } else if (op == "display") {
      display.displayBuffer(static_cast<FreeInkDisplay::RefreshMode>(a), false);
    } else if (op == "display-off") {
      display.displayBuffer(static_cast<FreeInkDisplay::RefreshMode>(a), true);
    } else if (op == "trigger") {
      display.triggerDisplay(static_cast<FreeInkDisplay::RefreshMode>(a));
    } else if (op == "complete") {
      display.completeDisplay();
    } else if (op == "trigger-async") {
      display.triggerDisplayAsync(static_cast<FreeInkDisplay::RefreshMode>(a));
    } else if (op == "finish-async") {
      display.finishDisplayAsync();
      display.completeDisplay();
    } else if (op == "async") {
      display.displayBufferAsync(static_cast<FreeInkDisplay::RefreshMode>(a));
    } else if (op == "async-no-shadow") {
      display.displayBufferAsyncNoShadow(static_cast<FreeInkDisplay::RefreshMode>(a));
    } else if (op == "wait") {
      display.waitRefreshComplete();
    } else if (op == "poll") {
      trace::event("busy " + std::to_string(display.refreshBusy()));
    } else if (op == "window") {
      if (a)
        display.displayWindow(16, 17, 88, 33);
      else
        display.displayWindow(13, 17, 91, 33);
    } else if (op == "invert") {
      display.setInverted(a);
    } else if (op == "resync") {
      display.requestResync(a);
    } else if (op == "condition") {
      display.preconditionGrayscale(16, 21, 80, 37);
    } else if (op == "gray-base") {
      const auto cap =
          display.grayscaleCapabilities(a ? freeink::GrayscaleMode::Absolute : freeink::GrayscaleMode::Overlay);
      trace::event("gray.cap encoding=" + std::to_string(static_cast<int>(cap.encoding)) +
                   " strips=" + std::to_string(cap.stripUploads) + " async=" + std::to_string(cap.asyncBase));
      const bool accepted =
          display.displayGrayscaleBase(a ? freeink::GrayscaleMode::Absolute : freeink::GrayscaleMode::Overlay);
      trace::event("gray.accepted " + std::to_string(accepted));
    } else if (op == "gray-full") {
      display.copyGrayscaleBuffers(lsb.data(), msb.data());
    } else if (op == "gray-strips") {
      for (const auto plane : {FreeInkDisplay::GRAY_PLANE_LSB, FreeInkDisplay::GRAY_PLANE_MSB}) {
        const auto& data = plane == FreeInkDisplay::GRAY_PLANE_LSB ? lsb : msb;
        for (uint16_t y = 0; y < display.getDisplayHeight(); y += 19) {
          display.writeGrayscalePlaneStrip(plane, data.data() + y * display.getDisplayWidthBytes(), y,
                                           std::min<unsigned>(19, display.getDisplayHeight() - y));
        }
      }
    } else if (op == "gray-show") {
      display.prepareGrayscaleTarget();
      display.displayGrayBuffer();
    } else if (op == "cleanup") {
      display.cleanupGrayscaleBuffers(bw.data());
    } else if (op == "seed") {
      display.syncRedRamFromFrameBuffer();
    } else if (op == "lend") {
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
      uint32_t n = 0;
      auto* storage = display.lendBuildStorage(&n);
#else
      size_t n = 0;
      auto* storage = display.borrowSecondaryBuffer(&n);
#endif
      trace::event("lent " + std::to_string(storage ? n : 0));
      if (storage) memset(storage, 0x6d, n);
    } else if (op == "return") {
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
      display.returnBuildStorage();
#else
      display.returnSecondaryBuffer();
#endif
    } else if (op == "release-secondary") {
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
      display.releaseSecondaryBuffer();
      display.setSingleBufferFastDiff(true);
#endif
    } else if (op == "realloc-secondary") {
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
      if (!display.reallocSecondaryBuffer()) return 4;
#endif
    } else if (op == "release") {
      display.releaseBuffers();
    } else if (op == "realloc") {
      if (!display.reallocBuffers()) return 4;
    } else if (op == "sleep") {
      display.deepSleep();
    } else if (op == "maintenance") {
      display.runMaintenance();
      display.controllerIdle();
    } else {
      std::cerr << "unknown operation: " << op << '\n';
      return 2;
    }
  }
  display.releaseBuffers();
  trace::finish();
}
