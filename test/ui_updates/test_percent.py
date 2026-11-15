from pathlib import Path
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent

class Percent(unittest.TestCase):
    def test_original(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'percent'
            subprocess.run(['c++', '-std=c++17', str(HERE / 'percent.cpp'), '-o', str(exe)], check=True)
            self.assertEqual(subprocess.check_output([str(exe)]), (HERE / 'percent.golden').read_bytes())

if __name__ == '__main__':
    unittest.main()
