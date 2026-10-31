"""Pinned native tab-band geometry and SDK label/indicator placements."""
import gzip
import re
import subprocess
import tempfile
import unittest
from pathlib import Path
from input_oracle import original, method
from test_oracle import HERE, SDK


def record_tabs(mutation=False):
    source = (HERE.parent / "reader_chrome/widgets.cpp").read_text().split("// SDK placements")[0]
    source += (HERE / "tabs_leaves.cpp").read_text()
    source += method(original("src/activities/UiTabListActivity.cpp"), "void UiTabListActivity::buildTabBar(")
    source += (HERE / "tabs_cases.cpp").read_text()
    if mutation:
        source, count = re.subn(r"tabPillMaxPad\s*=\s*16", "tabPillMaxPad = 17", source)
        assert count == 1, "tab pill mutation was not applied"
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "tabs.cpp").write_text(source)
        subprocess.run(["c++", "-std=c++20", "-DFREEINK_UI_COMPONENT_STYLE=0", "-I"+str(SDK/"include"), str(path/"tabs.cpp"), str(SDK/"src/FreeInkUI.cpp"), "-o",str(path/"tabs")],check=True)
        return subprocess.check_output([str(path/"tabs")])


class Tabs(unittest.TestCase):
    def test_native_tabs(self):
        self.assertEqual(record_tabs(), gzip.decompress((HERE/"tabs.golden.gz").read_bytes()))
    def test_pill_width_mutation(self):
        self.assertNotEqual(record_tabs(True), gzip.decompress((HERE/"tabs.golden.gz").read_bytes()))

if __name__ == "__main__":
    unittest.main()
