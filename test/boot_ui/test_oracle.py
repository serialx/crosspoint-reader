"""Frozen pre-port behavior and a deliberate mutation sensitivity check."""
import pathlib
import subprocess
import tempfile
import unittest
HERE = pathlib.Path(__file__).resolve().parent

def record(mutate=False):
    with tempfile.TemporaryDirectory() as work:
        root = pathlib.Path(work)
        source = (HERE / 'baseline.inc').read_text()
        if mutate:
            source = source.replace('pageHeight / 2 + 70', 'pageHeight / 2 + 71')
        (root / 'baseline.inc').write_text(source)
        (root / 'oracle.cpp').write_bytes((HERE / 'oracle.cpp').read_bytes())
        subprocess.run(['c++', '-std=c++20', str(root/'oracle.cpp'), '-o', str(root/'oracle')], check=True)
        return subprocess.check_output([str(root/'oracle')])

class Oracle(unittest.TestCase):
    def test_frozen_cpp(self):
        self.assertEqual(record(), (HERE/'policy.golden').read_bytes())
    def test_rejects_one_pixel_mutation(self):
        self.assertNotEqual(record(True), (HERE/'policy.golden').read_bytes())

if __name__ == '__main__':
    unittest.main()
