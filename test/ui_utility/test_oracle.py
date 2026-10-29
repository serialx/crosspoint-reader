"""Utility widget records frozen from the pinned SDK before porting."""
from pathlib import Path
import subprocess
import tempfile
import unittest
HERE = Path(__file__).resolve().parent
SDK = HERE.parents[1] / "freeink-sdk/libs/ui/FreeInkUI"
def record(source=None):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        cpp = HERE / "oracle.cpp"
        if source is not None:
            cpp = root / "oracle.cpp"
            cpp.write_text(source)
        subprocess.run(["c++", "-std=c++20", "-DFREEINK_UI_COMPONENT_STYLE=0", "-I"+str(SDK / "include"), str(cpp), str(SDK / "src/FreeInkUI.cpp"), "-o", str(root / "oracle")], check=True)
        return subprocess.check_output([str(root / "oracle")])
class Golden(unittest.TestCase):
    def test_cpp_widgets_and_input(self):
        self.assertEqual(record(), (HERE / "widgets.golden").read_bytes())
    def test_application_input(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "input"
            subprocess.run(["c++", "-std=c++20", str(HERE / "input_oracle.cpp"), "-o", str(exe)], check=True)
            self.assertEqual(subprocess.check_output([str(exe)]), (HERE / "input.golden").read_bytes())
    def test_mutation_is_detected(self):
        original = (HERE / "oracle.cpp").read_text()
        changed = original.replace("p.buttonHeight = 40;", "p.buttonHeight = 41;")
        self.assertNotEqual(original, changed)
        self.assertNotEqual(record(changed), (HERE / "widgets.golden").read_bytes())
if __name__ == "__main__":
    unittest.main()
