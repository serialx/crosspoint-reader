"""Exercise the firmware output methods with a recording display and LED driver."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/reader_lifecycle'))
from extract import definition


class FrontlightOutput(unittest.TestCase):
    def compile_run(self, source):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'output.cpp'
            binary = path.with_suffix('')
            path.write_text('#include <cassert>\n#include <cstdint>\n#include <vector>\n' + source)
            subprocess.run(['c++', '-std=c++20', str(path), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_grayscale_consumes_refresh_promotion_once(self):
        source = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
        methods = '\n'.join(definition(source, marker) for marker in (
            'HalDisplay::RefreshMode GfxRenderer::applyPromotedRefresh(',
            'void GfxRenderer::displayGrayscaleBase(',
            'bool GfxRenderer::displayGrayscaleBase(',
        ))
        self.compile_run(r'''
struct HalDisplay {
  enum RefreshMode { FAST_REFRESH, FULL_REFRESH };
  enum class GrayscaleMode { Overlay, Absolute };
  std::vector<RefreshMode> modes;
  bool accepts = true;
  void displayGrayscaleBase(RefreshMode mode, bool) { modes.push_back(mode); }
  bool displayGrayscaleBase(GrayscaleMode, RefreshMode mode, bool) {
    modes.push_back(mode);
    return accepts;
  }
};
class GfxRenderer {
 public:
  mutable HalDisplay display;
  mutable bool absoluteGrayPlanes = false;
  mutable bool promotedRefreshPending_ = true;
  HalDisplay::RefreshMode promotedRefresh_ = HalDisplay::FULL_REFRESH;
  bool fadingFix = false;
  HalDisplay::RefreshMode applyPromotedRefresh(HalDisplay::RefreshMode) const;
  void displayGrayscaleBase(HalDisplay::RefreshMode) const;
  bool displayGrayscaleBase(HalDisplay::GrayscaleMode, HalDisplay::RefreshMode) const;
};
''' + methods + r'''
int main() {
  using H = HalDisplay;
  GfxRenderer renderer;
  renderer.displayGrayscaleBase(H::FAST_REFRESH);
  renderer.displayGrayscaleBase(H::FAST_REFRESH);
  assert((renderer.display.modes == std::vector{H::FULL_REFRESH, H::FAST_REFRESH}));
  renderer.display.modes.clear();
  renderer.promotedRefreshPending_ = true;
  renderer.display.accepts = false;
  assert(!renderer.displayGrayscaleBase(H::GrayscaleMode::Absolute, H::FAST_REFRESH));
  renderer.display.accepts = true;
  assert(renderer.displayGrayscaleBase(H::GrayscaleMode::Absolute, H::FAST_REFRESH));
  assert(renderer.displayGrayscaleBase(H::GrayscaleMode::Absolute, H::FAST_REFRESH));
  assert((renderer.display.modes == std::vector{H::FULL_REFRESH, H::FULL_REFRESH, H::FAST_REFRESH}));
}
''')

    def test_bluetooth_touch_panel_with_two_sliders_reserves_full_scene(self):
        source = (ROOT / 'src/activities/util/FrontlightPanelActivity.cpp').read_text()
        self.compile_run(r'''
struct HalLight {
  bool available, warm;
  bool present() const { return available; }
  bool hasColorTemperature() const { return warm; }
  int brightness() const { return 60; }
  int warmth() const { return 50; }
  bool isOn() const { return false; }
} Frontlight;
namespace bleturner {
struct Status { bool compiledIn; } radio;
Status status() { return radio; }
}
struct {
  struct { bool enabled = false; } ble;
  int screenInverted = 0, orientation = 0, touchReaderControls = 0;
} SETTINGS;
bool slideSwitchOwnsLight() { return false; }
struct UtilityActivity {
  static constexpr uint16_t BaseCommands = 43, BaseHits = 11;
  uint16_t commandCapacity = BaseCommands, hitCapacity = BaseHits;
  uint16_t reservedCommands = 0, reservedHits = 0;
  void onEnter() { reservedCommands = commandCapacity; reservedHits = hitCapacity; }
};
struct FrontlightPanelActivity : UtilityActivity {
  struct {
    bool light_unavailable, ble_compiled, ble_enabled;
    int brightness, warmth, light_on, inverted, orientation, touch_mode, switch_owns_light;
  } state{};
  struct { bool touch; bool hasTouch() const { return touch; } } mappedInput{};
  void onEnter();
};
''' + definition(source, 'void FrontlightPanelActivity::onEnter(') + r'''
int main() {
  FrontlightPanelActivity panel;
  for (unsigned flags = 16; flags-- > 0;) {
    Frontlight.available = flags & 1;
    Frontlight.warm = flags & 2;
    panel.mappedInput.touch = flags & 4;
    bleturner::radio.compiledIn = flags & 8;
    panel.onEnter();
    assert(panel.reservedCommands == (flags == 15 ? 44 : 43));
    assert(panel.reservedHits == (flags == 15 ? 12 : 11));
  }
}
''')


if __name__ == '__main__':
    unittest.main()
