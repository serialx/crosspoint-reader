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

    def test_wake_sets_brightness_before_warmth(self):
        source = (ROOT / 'lib/hal/HalFrontlight.cpp').read_text()
        self.compile_run(r'''
#define LOG_INF(...) ((void)0)
struct Light { uint8_t brightness = 0; bool lit = false; };
uint8_t xp_hal_percent(uint8_t input) { return input; }
void xp_hal_light_brightness(Light* s, uint8_t input) { s->brightness = input; }
void xp_hal_light_on(Light* s, bool input) { s->lit = input; }
uint8_t xp_hal_light_output(const Light* s) { return s->lit ? s->brightness : 0; }
struct Manager {
  std::vector<int> writes;
  void begin() { writes.push_back(-1); }
  bool present() const { return true; }
  void setBrightness(uint8_t level) { writes.push_back(level); }
  void setColorTemperature(uint8_t warmth) { writes.push_back(100 + warmth); }
};
struct HalFrontlight {
  Manager manager;
  Light state;
  void begin(uint8_t brightness, uint8_t warmth, bool on);
};
''' + definition(source, 'void HalFrontlight::begin(') + r'''
int main() {
  for (bool on : {false, true}) {
    HalFrontlight light;
    light.begin(60, 50, on);
    assert((light.manager.writes == std::vector{-1, on ? 60 : 0, 150}));
  }
}
''')

    def test_physical_switch_controls_initial_wake_state(self):
        source = (ROOT / 'src/main.cpp').read_text()
        start = source.index('  const bool restoreLightOn =')
        restore = source[start:source.index('  Frontlight.begin(', start)]
        switch = definition(source, 'static void applySlideSwitchAtWake(')
        self.assertLess(source.index('  applySlideSwitchAtWake();'), source.index('  setupDisplayAndFonts(resume'))
        self.compile_run(r'''
struct CrossPointSettings {
  enum Action { SLIDE_SWITCH_FRONTLIGHT, SLIDE_SWITCH_NIGHT_MODE, SLIDE_SWITCH_TOUCH_CONTROLS };
  enum { TOUCH_READER_OFF, TOUCH_READER_ON };
  int slideSwitchAction = SLIDE_SWITCH_FRONTLIGHT;
  uint8_t frontlightOn = 0, frontlightRestoreOnWake = 0, screenInverted = 0, touchReaderControls = 1;
  int saves = 0;
  void saveToFile() { ++saves; }
} SETTINGS;
struct {
  bool present = true, on = false;
  bool hasToggleSwitch() const { return present; }
  bool isToggleSwitchOn() const { return on; }
} gpio;
struct {
  bool on = false;
  bool present() const { return true; }
  bool isOn() const { return on; }
  void setOn(bool enabled) { on = enabled; }
} Frontlight;
bool slideSwitchOwnsLight() { return gpio.present && SETTINGS.slideSwitchAction == CrossPointSettings::SLIDE_SWITCH_FRONTLIGHT; }
bool initialLight(bool isSilentReboot, bool silentRebootLightOn) {
''' + restore + r'''
  return restoreLightOn;
}
''' + switch + r'''
int main() {
  gpio.on = true;
  assert(initialLight(false, false));
  assert(!initialLight(true, false));
  Frontlight.on = initialLight(false, false);
  applySlideSwitchAtWake();
  assert(SETTINGS.frontlightOn && SETTINGS.saves == 1);
  applySlideSwitchAtWake();
  assert(SETTINGS.saves == 1);
  gpio.on = false;
  applySlideSwitchAtWake();
  assert(!Frontlight.on && !SETTINGS.frontlightOn && SETTINGS.saves == 2);
  SETTINGS.slideSwitchAction = CrossPointSettings::SLIDE_SWITCH_NIGHT_MODE;
  gpio.on = true;
  assert(!initialLight(false, false));
  applySlideSwitchAtWake();
  assert(SETTINGS.screenInverted && !Frontlight.on && SETTINGS.saves == 3);
  SETTINGS.slideSwitchAction = CrossPointSettings::SLIDE_SWITCH_TOUCH_CONTROLS;
  gpio.on = false;
  applySlideSwitchAtWake();
  assert(!SETTINGS.touchReaderControls && SETTINGS.saves == 4);
  gpio.present = false;
  gpio.on = true;
  applySlideSwitchAtWake();
  assert(!SETTINGS.touchReaderControls && SETTINGS.saves == 4);
}
''')


if __name__ == '__main__':
    unittest.main()
