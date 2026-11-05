"""Pinned CoverGridHomeUi outer layout and SDK widgets, before Rust replacement."""
import gzip
import subprocess
import tempfile
import unittest
from pathlib import Path
from input_oracle import original, method
from test_oracle import HERE, SDK


def record_grid(mutation=False):
    source = (HERE.parent / "reader_chrome/widgets.cpp").read_text().split("// SDK placements")[0]
    source += (HERE / "grid_leaves.cpp").read_text()
    native = original("src/components/CoverGridHomeUi.cpp")
    for signature in ["void CoverGridHomeUi::draw(", "void CoverGridHomeUi::drawHeaderBand(",
                      "void CoverGridHomeUi::drawEmpty(", "void CoverGridHomeUi::drawCurrent(",
                      "fui::Rect CoverGridHomeUi::layoutGrid(", "void CoverGridHomeUi::drawGrid(",
                      "void CoverGridHomeUi::drawTabs(", "bool CoverGridHomeUi::paintFramedCover("]:
        source += method(native, signature)
    source += (HERE / "grid_cases.cpp").read_text()
    if mutation:
        source = source.replace("COVER_CELL_INSET = 6", "COVER_CELL_INSET = 7")
        source = source.replace("COVER_CELL_INSET=6", "COVER_CELL_INSET=7")
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "grid.cpp").write_text(source)
        subprocess.run(["c++", "-std=c++20", "-DFREEINK_UI_COMPONENT_STYLE=0", "-I" + str(SDK / "include"),
                        str(path / "grid.cpp"), str(SDK / "src/FreeInkUI.cpp"), "-o", str(path / "grid")], check=True)
        return subprocess.check_output([str(path / "grid")])


class Grid(unittest.TestCase):
    def test_native_grid(self):
        self.assertEqual(record_grid(), gzip.decompress((HERE / "grid.golden.gz").read_bytes()))

    def test_inset_mutation(self):
        self.assertNotEqual(record_grid(True), gzip.decompress((HERE / "grid.golden.gz").read_bytes()))


if __name__ == "__main__":
    unittest.main()
