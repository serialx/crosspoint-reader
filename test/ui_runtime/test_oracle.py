"""Frozen SDK list geometry and single-line glyph origins, before the Rust port."""
import pathlib
import subprocess
import tempfile
import unittest
import geometry_oracle

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SDK = ROOT / "freeink-sdk/libs/ui/FreeInkUI"


def record(source=None):
    with tempfile.TemporaryDirectory() as directory:
        executable = pathlib.Path(directory) / "oracle"
        cpp = HERE / "oracle.cpp"
        if source is not None:
            cpp = pathlib.Path(directory) / "oracle.cpp"
            cpp.write_text(source)
        subprocess.run(["c++", "-std=c++20", "-DFREEINK_UI_COMPONENT_STYLE=0",
                        "-I" + str(SDK / "include"), str(cpp),
                        str(SDK / "src/FreeInkUI.cpp"), "-o", str(executable)], check=True)
        return subprocess.check_output([str(executable)])


class Golden(unittest.TestCase):
    def test_cpp_widget_pixels(self):
        self.assertEqual(geometry_oracle.record(), (HERE / "geometry.golden").read_bytes())

    def test_top_corner_pixels(self):
        self.assertEqual(geometry_oracle.record(True), (HERE / "top-corners.golden").read_bytes())

    def test_message_layout(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / "message"
            subprocess.run(["c++", str(HERE / "message_oracle.cpp"), "-o", str(binary)], check=True)
            self.assertEqual(subprocess.check_output([str(binary)]), (HERE / "message.golden").read_bytes())

    def test_sdk_layout(self):
        self.assertEqual(record(), (HERE / "layout.golden").read_bytes())

    def test_rejects_radius_mutation(self):
        source = (HERE / "oracle.cpp").read_text().replace("theme.listRowRadius = 12;", "theme.listRowRadius = 13;")
        self.assertNotEqual(record(source), (HERE / "layout.golden").read_bytes())


if __name__ == "__main__":
    unittest.main()
