"""Run with python3 test/test_clipping_highlight_planes.py (requires a host C++ compiler)."""

from pathlib import Path
import subprocess
import tempfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from rust_host import rust_link_flags

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text()
if 'const auto fillHighlight =' in source:
    fill = "const auto fillHighlight =" + source.split("const auto fillHighlight =", 1)[1].split("\n  uint16_t pageWordIndex", 1)[0]
    flags = []
else:
    fill = (ROOT / 'test/reader_lifecycle/clipping_highlight_rust.inc').read_text()
    flags = rust_link_flags()
harness = r"""
#if __has_include(<xp_ui_reader.h>)
#include <xp_ui_reader.h>
#endif
#include <cassert>
#include <memory>
struct BoardConfig {
  static inline bool a4 = false;
  static bool isEegoA4() { return a4; }
};
enum class Color { LightGray };
struct GfxRenderer {
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  RenderMode mode = BW;
  unsigned dots = 0, ditherRects = 0;
  RenderMode getRenderMode() const { return mode; }
  void drawPixel(int x, int y, bool state) {
    assert((x & 1) == 0 && (y & 1) == 0 && !state);
    ++dots;
  }
  void fillRectDither(int, int, int, int, Color) { ++ditherRects; }
};
int main() {
  GfxRenderer renderer;
""" + fill + r"""
  for (bool a4 : {false, true}) {
    BoardConfig::a4 = a4;
    for (auto mode : {GfxRenderer::BW, GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      renderer.mode = mode;
      renderer.dots = renderer.ditherRects = 0;
      fillHighlight(1, 1, 4, 4);
      assert(renderer.dots == (a4 && mode != GfxRenderer::BW ? 4u : 0u));
      assert(renderer.ditherRects == (a4 && mode != GfxRenderer::BW ? 0u : 1u));
    }
  }

}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "highlight.cpp"
    executable = Path(directory) / "highlight"
    cpp.write_text(harness)
    subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", str(cpp), *flags, "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
# Grayscale operation order is frozen by reader_render_oracle_off/on, which
# now executes the Rust plan through the extracted production resource binding.
print("Clipping highlight plane regression checks passed")
