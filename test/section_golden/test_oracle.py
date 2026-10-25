"""Verify that corpus comparison fails closed when output changes."""

from pathlib import Path
import tempfile
import unittest

from run import compare, read_archive, write_archive


class OracleTest(unittest.TestCase):
    def setUp(self):
        self.sections = {"book/0/0.bin": b"\x44\x00\x01\x02", "book/1/0.bin": b"\x44\x03"}

    def test_archive_preserves_complete_binary_files(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "sections.tar.gz"
            write_archive(path, self.sections)
            original = path.read_bytes()
            compare(self.sections, read_archive(path))
            write_archive(path, dict(reversed(list(self.sections.items()))))
            self.assertEqual(original, path.read_bytes())

    def test_rejects_byte_mutation(self):
        changed = dict(self.sections)
        changed["book/0/0.bin"] = b"\x44\x00\xff\x02"
        with self.assertRaisesRegex(ValueError, "first byte offset 2"):
            compare(self.sections, changed)

    def test_rejects_truncation_and_trailing_bytes(self):
        for data in (b"\x44\x00", b"\x44\x00\x01\x02\x03"):
            changed = dict(self.sections)
            changed["book/0/0.bin"] = data
            with self.assertRaisesRegex(ValueError, "Section differs"):
                compare(self.sections, changed)

    def test_rejects_missing_or_extra_cases(self):
        missing = dict(self.sections)
        del missing["book/1/0.bin"]
        extra = dict(self.sections)
        extra["book/2/0.bin"] = b"\x44"
        for changed in (missing, extra):
            with self.assertRaisesRegex(ValueError, "Section set changed"):
                compare(self.sections, changed)


if __name__ == "__main__":
    unittest.main()
