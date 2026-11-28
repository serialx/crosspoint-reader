"""The private family wire layout must match on C++ and Rust; mutations fail closed."""
from pathlib import Path
import subprocess
import tempfile
import unittest
HERE = Path(__file__).resolve().parent
INCLUDE = HERE.parents[1] / 'rust/crates/xp-ffi/include'


def compile_header(change=None):
    with tempfile.TemporaryDirectory() as directory:
        target = Path(directory)
        header = (INCLUDE / 'xp_ui_updates.h').read_text()
        if change:
            header = header.replace(*change)
        (target / 'xp_ui_updates.h').write_text(header)
        return subprocess.run(['c++', '-std=c++20', '-I'+str(target), '-I'+str(INCLUDE),
                               '-c', str(HERE / 'abi.cpp'), '-o', str(target/'abi.o')],
                              capture_output=True, text=True)


class Abi(unittest.TestCase):
    def test_c_header(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)
            source = target / 'abi.c'
            source.write_text('#include "xp_ui_updates.h"\n')
            result = subprocess.run(['cc', '-std=c11', '-I'+str(INCLUDE), '-c', str(source), '-o', str(target/'abi.o')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_layout_and_values(self):
        result=compile_header()
        self.assertEqual(result.returncode,0,result.stderr)

    def test_value_drift(self):
        self.assertNotEqual(compile_header(('XP_UI_UPDATE_PROGRESS = 8', 'XP_UI_UPDATE_PROGRESS = 80')).returncode,0)

    def test_layout_drift(self):
        self.assertNotEqual(compile_header(('processed, total', 'total, processed')).returncode,0)


if __name__ == '__main__':
    unittest.main()
