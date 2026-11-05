"""Freeze cache-slot decisions and the native bitmap failure/placeholder leaf."""
import gzip
from pathlib import Path
import subprocess
import tempfile
import unittest
from input_oracle import method, original

HERE = Path(__file__).resolve().parent


def record_cache(mutation=False):
    native = original("src/components/HomeCoverCache.cpp")
    source = (HERE / "cache_leaves.cpp").read_text()
    for signature in ["void HomeCoverCache::begin()", "void HomeCoverCache::prepare()",
                      "void HomeCoverCache::invalidate()", "void HomeCoverCache::invalidate(size_t",
                      "bool HomeCoverCache::paint("]:
        source += method(native, signature)
    source += (HERE / "cache_cases.cpp").read_text()
    if mutation:
        source = source.replace("if (!drawn)", "if (false)")
    with tempfile.TemporaryDirectory() as directory:
        out = Path(directory)
        (out / "cache.cpp").write_text(source)
        subprocess.run(["c++", "-std=c++20", str(out / "cache.cpp"), "-o", str(out / "cache")], check=True)
        return subprocess.check_output([str(out / "cache")])


class CoverCache(unittest.TestCase):
    def test_native_slots_and_placeholder(self):
        self.assertEqual(record_cache(), gzip.decompress((HERE / "cache.golden.gz").read_bytes()))

    def test_removed_placeholder_mutation(self):
        self.assertNotEqual(record_cache(True), gzip.decompress((HERE / "cache.golden.gz").read_bytes()))


if __name__ == "__main__":
    unittest.main()
