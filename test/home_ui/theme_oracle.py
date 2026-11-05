"""Native Home theme calls, including snapshot allocation failure, from the pinned baseline."""
import gzip
from pathlib import Path
import subprocess
import tempfile
import unittest

from input_oracle import original, method

HERE = Path(__file__).resolve().parent


def record_theme(mutation=False):
    source = (HERE / "theme_leaves.cpp").read_text()
    for name, path, signatures in [
        ("BaseTheme", "BaseTheme", ["drawRecentBookCover", "drawButtonMenu"]),
        ("LyraTheme", "lyra/LyraTheme", ["drawRecentBookCover", "drawEmptyRecents", "drawButtonMenu"]),
        ("Lyra3CoversTheme", "lyra/Lyra3CoversTheme", ["homeCoverThumbHeight", "drawRecentBookCover"]),
        ("RoundedRaffTheme", "roundedraff/RoundedRaffTheme", ["drawRecentBookCover", "getMenuRowHeight", "drawButtonMenu"]),
    ]:
        native = original(f"src/components/themes/{path}.cpp")
        if name == "RoundedRaffTheme":
            source += method(native, "void drawScrollBar(")
        for signature in signatures:
            result = "int" if signature in ("homeCoverThumbHeight", "getMenuRowHeight") else "void"
            source += method(native, f"{result} {name}::{signature}(")
    source += (HERE / "theme_cases.cpp").read_text()
    if mutation:
        source = source.replace("boxPadding = 8", "boxPadding = 9")
    with tempfile.TemporaryDirectory() as directory:
        out = Path(directory)
        (out / "theme.cpp").write_text(source)
        subprocess.run(["c++", "-std=c++20", str(out / "theme.cpp"), "-o", str(out / "theme")], check=True)
        return subprocess.check_output([str(out / "theme")])


class HomeTheme(unittest.TestCase):
    def test_native_theme(self):
        self.assertEqual(record_theme(), gzip.decompress((HERE / "theme.golden.gz").read_bytes()))

    def test_title_box_mutation(self):
        self.assertNotEqual(record_theme(True), gzip.decompress((HERE / "theme.golden.gz").read_bytes()))


if __name__ == "__main__":
    unittest.main()
