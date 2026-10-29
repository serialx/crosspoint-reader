"""The Section image callback can run after the render frame has expired."""
from pathlib import Path
import re
import subprocess
import tempfile

from extract import definition

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
callback = re.search(r'return o.section->startBuild\(spec, (\[owner = &o\] \{.*?\})\);', source)[1]
helper = definition(source, 'void EpubReaderActivity::showBuildPopup(')
fixture = r'''
#include <cassert>
#include <cstdint>
#include <functional>
struct Renderer { bool framebuffer = true; bool hasFrameBuffer() { return framebuffer; } };
struct Gui { unsigned paints = 0; void drawPopup(Renderer&, int) { ++paints; } } GUI;
constexpr int STR_INDEXING = 1;
int tr(int value) { return value; }
struct EpubReaderActivity {
  Renderer renderer;
  bool buildPopupPending = false;
  int32_t* renderFrame = nullptr;
  int32_t& renderRefreshCounter() { assert(renderFrame); return *renderFrame; }
  void showBuildPopup();
  std::function<void()> imageProbe() { auto& o = *this; return CALLBACK; }
};
HELPER
int main() {
  EpubReaderActivity reader;
  auto retained = reader.imageProbe();
  int32_t counter = 8;
  reader.renderFrame = &counter;
  reader.buildPopupPending = true;
  retained();
  assert(GUI.paints == 1 && counter == 1 && !reader.buildPopupPending);
  reader.renderFrame = nullptr;
  // Background build holds the resource lock, but has no render model frame.
  retained();
  assert(GUI.paints == 1);
  reader.renderer.framebuffer = false;
  reader.buildPopupPending = true;
  retained();
  assert(GUI.paints == 1 && reader.buildPopupPending);
}
'''.replace('CALLBACK', callback).replace('HELPER', helper)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    cpp = path / 'popup.cpp'
    cpp.write_text(fixture)
    flags = ['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined',
             '-fno-sanitize-recover=all', str(cpp), '-o', str(path / 'popup')]
    subprocess.run(flags, check=True)
    subprocess.run([str(path / 'popup')], check=True)
    # The previous eager argument lookup must fail on the retained callback.
    cpp.write_text(fixture.replace(callback, '[this] { (void)renderRefreshCounter(); showBuildPopup(); }'))
    subprocess.run(flags, check=True)
    failed = subprocess.run([str(path / 'popup')], capture_output=True, text=True)
    assert failed.returncode != 0 and 'renderFrame' in failed.stderr
print('retained build-popup callback: active render, background probe, unavailable framebuffer, and eager-lookup mutation pass')
