"""Compare the Rust family directly with independently frozen native traces."""
import gzip
from pathlib import Path
import subprocess
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def rust_trace(binary):
    return subprocess.run(
        ["cargo", "run", "--offline", "--quiet", "--manifest-path", str(HERE / "rust/Cargo.toml"), "--bin", binary],
        cwd=ROOT / "rust", check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    ).stdout


class RustDifferential(unittest.TestCase):
    def test_touch(self):
        self.assertEqual(rust_trace("home-touch"), gzip.decompress((HERE / "touch.golden.gz").read_bytes()))

    def test_file_library_rows(self):
        native = gzip.decompress((HERE / "widgets.golden.gz").read_bytes()).split(b"covers ")[0]
        self.assertEqual(rust_trace("home-rows"), native)

    def test_library_tabs(self):
        self.assertEqual(rust_trace("home-tabs"), gzip.decompress((HERE / "tabs.golden.gz").read_bytes()))

    def test_cover_grid(self):
        self.assertEqual(rust_trace("home-grid"), gzip.decompress((HERE / "grid.golden.gz").read_bytes()))

    def test_cover_cache(self):
        self.assertEqual(rust_trace("cover-cache"), gzip.decompress((HERE / "cache.golden.gz").read_bytes()))

    def test_home_input(self):
        self.assertEqual(rust_trace("home-input"), gzip.decompress((HERE / "home-input.golden.gz").read_bytes()))

    def test_standard_home_view(self):
        self.assertEqual(rust_trace("home-view"), gzip.decompress((HERE / "theme.golden.gz").read_bytes()))

    def test_file_input(self):
        self.assertEqual(rust_trace("file-input"), gzip.decompress((HERE / "file-input.golden.gz").read_bytes()))

    def test_library_input(self):
        self.assertEqual(rust_trace("library-input"), gzip.decompress((HERE / "library-input.golden.gz").read_bytes()))


if __name__ == "__main__":
    unittest.main()
