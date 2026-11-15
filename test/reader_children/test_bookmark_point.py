"""Exercise the physical point leaf with the actual mapped-input and orientation code."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/reader_lifecycle'))
from extract import definition


def source(path, signature):
    return definition((ROOT / path).read_text(), signature)


prefix = r'''
#include <FreeInkUI.h>
#include <cassert>
#include <cstdint>
namespace fui = freeink::ui;
struct GfxRenderer {
  enum Orientation { Portrait, PortraitInverted, LandscapeClockwise, LandscapeCounterClockwise };
  int panelWidth = 792, panelHeight = 528;
  Orientation orientation = Portrait;
  void tapToLogical(float, float, int&, int&) const;
};
struct GPIO {
  float sx = .25f, sy = .25f, ex = .75f, ey = .75f;
  bool swipe = true, held = false;
  mutable unsigned suppressed = 0;
  bool wasSwipe(float& x, float& y, float& endX, float& endY) const {
    x = sx; y = sy; endX = ex; endY = ey; return swipe;
  }
  bool wasTouchLongPress(float& x, float& y) const { x = sx; y = sy; return held; }
  void suppressTouchContact() const { ++suppressed; }
};
struct MappedInputManager {
  enum class SwipeDir { None, Left, Right, Up, Down };
  GPIO& gpio;
  GfxRenderer& renderer;
  bool decodeSwipe(int&, int&, int&, int&) const;
  SwipeDir wasSwipe(int* = nullptr, int* = nullptr) const;
  bool wasScreenLongPress(int&, int&) const;
  bool isScreenTouchHeld(int&, int&) const { return false; }
  bool wasScreenTouchDown(int&, int&) const { return false; }
  bool wasScreenTapped(int&, int&) const { return false; }
  bool wasScreenTouchReleased() const { return true; }
};
static MappedInputManager* mapped;
'''

methods = '\n'.join([
    source('lib/GfxRenderer/GfxRenderer.cpp', 'void GfxRenderer::tapToLogical('),
    source('src/MappedInputManager.cpp', 'bool MappedInputManager::decodeSwipe('),
    source('src/MappedInputManager.cpp', 'MappedInputManager::SwipeDir MappedInputManager::wasSwipe('),
    source('src/MappedInputManager.cpp', 'bool MappedInputManager::wasScreenLongPress('),
    source('src/components/UiAppHelpers.h', 'inline freeink::ui::InputSnapshot touchSnapshotFrom('),
])
leaf = source('src/activities/reader/RustEpubResources.cpp', '.input_screen_point =')
leaf = 'static auto ' + leaf.replace('.input_screen_point =', 'point =', 1) + ';\n'
main = r'''
int main() {
  GPIO gpio;
  GfxRenderer renderer;
  MappedInputManager input{gpio, renderer};
  mapped = &input;
  const int expected[][2] = {{395,198},{132,593},{593,395},{198,132}};
  for (int orientation = 0; orientation < 4; ++orientation) {
    renderer.orientation = static_cast<GfxRenderer::Orientation>(orientation);
    const auto snapshot = touchSnapshotFrom(input);
    assert(snapshot.touchReleased && snapshot.touchX == -1 && snapshot.touchY == -1);
    const auto direction = input.wasSwipe();
    int nativeX = -7, nativeY = -8, x = -9, y = -10;
    assert(input.wasSwipe(&nativeX, &nativeY) == direction);
    assert(point(1, &x, &y) == 1);
    assert(x == nativeX && y == nativeY);
    assert(x == expected[orientation][0] && y == expected[orientation][1]);
    assert(gpio.suppressed == 0 && input.wasSwipe() == direction);
    gpio.swipe = false;
    x = -9; y = -10;
    assert(point(1, &x, &y) == 0 && x == -9 && y == -10);
    assert(point(2, &x, &y) == 0 && x == -9 && y == -10);
    assert(point(0, &x, &y) == 0 && x == -9 && y == -10);
    gpio.held = true;
    assert(point(0, &x, &y) == 1);
    assert(x == nativeX && y == nativeY && gpio.suppressed == 1);
    gpio.held = false; gpio.swipe = true; gpio.suppressed = 0;
  }
}
'''
with tempfile.TemporaryDirectory(prefix='bookmark-point-') as directory:
    directory = Path(directory)
    for control in (False, True):
        cpp = directory / ('negative.cpp' if control else 'point.cpp')
        body = leaf.replace('*x = hitX;', '*x = -1;').replace('*y = hitY;', '*y = -1;') if control else leaf
        cpp.write_text(prefix + methods + body + main)
        binary = cpp.with_suffix('')
        subprocess.run(['c++', '-std=c++20', '-I' + str(ROOT / 'freeink-sdk/libs/ui/FreeInkUI/include'),
                        str(cpp), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True)
        assert (result.returncode != 0) == control, result.stderr.decode()
print('Physical swipe-start/long-press parity: four orientations; release-point negative control rejected')
