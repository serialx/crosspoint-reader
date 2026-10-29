"""Frozen reader list placements from the SDK pinned by 386e3c45."""
import gzip
import pathlib
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SDK = ROOT / "freeink-sdk/libs/ui/FreeInkUI"


def record(mutation=False):
    with tempfile.TemporaryDirectory() as directory:
        source = HERE / "widgets.cpp"
        if mutation:
            source = pathlib.Path(directory) / "widgets.cpp"
            source.write_text((HERE / "widgets.cpp").read_text().replace(
                "props.valueInset = 8;", "props.valueInset = 9;"))
        binary = pathlib.Path(directory) / "widgets"
        subprocess.run(["c++", "-std=c++20", "-DFREEINK_UI_COMPONENT_STYLE=0",
                        "-I" + str(SDK / "include"), str(source),
                        str(SDK / "src/FreeInkUI.cpp"), "-o", str(binary)], check=True)
        return subprocess.check_output([str(binary)])


class ReaderWidgets(unittest.TestCase):
    def test_footnote_input(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / "input"
            subprocess.run(["c++", "-std=c++20", str(HERE / "footnote_input.cpp"),
                            "-o", str(binary)], check=True)
            self.assertEqual(subprocess.check_output([str(binary)]),
                             gzip.decompress((HERE / "footnote-input.golden.gz").read_bytes()))

    def test_end_input(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / "end"
            subprocess.run(["c++", "-std=c++20", str(HERE / "end_input.cpp"), "-o", str(binary)], check=True)
            self.assertEqual(subprocess.check_output([str(binary)]),
                             gzip.decompress((HERE / "end-input.golden.gz").read_bytes()))

    def test_placements(self):
        self.assertEqual(record(), gzip.decompress((HERE / "widgets.golden.gz").read_bytes()))

    def test_value_inset_mutation_is_detected(self):
        self.assertNotEqual(record(True), gzip.decompress((HERE / "widgets.golden.gz").read_bytes()))


if __name__ == "__main__":
    unittest.main()
