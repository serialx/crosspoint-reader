"""SDK placements frozen before the Home/Library/FileBrowser Rust port."""
import gzip
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SDK = ROOT / "freeink-sdk/libs/ui/FreeInkUI"


def record(mutation=False):
    recorder = (HERE.parent / "reader_chrome/widgets.cpp").read_text().split("// SDK placements")[0]
    body = (HERE / "widgets.cpp").read_text()
    if mutation:
        body = body.replace("props.valueInset = 8;", "props.valueInset = 9;")
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "widgets.cpp"
        source.write_text(recorder + body)
        binary = Path(directory) / "widgets"
        subprocess.run(["c++", "-std=c++20", "-DFREEINK_UI_COMPONENT_STYLE=0",
                        "-I" + str(SDK / "include"), str(source),
                        str(SDK / "src/FreeInkUI.cpp"), "-o", str(binary)], check=True)
        return subprocess.check_output([str(binary)])


class HomeWidgets(unittest.TestCase):
    def test_placements(self):
        self.assertEqual(record(), gzip.decompress((HERE / "widgets.golden.gz").read_bytes()))

    def test_filename_value_inset_mutation(self):
        self.assertNotEqual(record(True), gzip.decompress((HERE / "widgets.golden.gz").read_bytes()))


if __name__ == "__main__":
    unittest.main()
