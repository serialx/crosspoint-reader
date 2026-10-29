"""Frozen catalog status/download geometry from the original C++ builders."""
import gzip
import pathlib
import subprocess
import tempfile
import unittest
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SDK = ROOT / 'freeink-sdk/libs/ui/FreeInkUI'

def record(mutate=False):
    with tempfile.TemporaryDirectory() as directory:
        source = (HERE / 'oracle.cpp').read_text()
        source = source.replace('../ui_settings/Recorder.h', str(HERE.parent / 'ui_settings/Recorder.h'))
        original = (HERE / 'status_original.inc').read_text()
        if mutate:
            original = original.replace('showBytes ? lh : 16', 'showBytes ? lh : 17')
        source = source.replace('#include "status_original.inc"', original)
        cpp = pathlib.Path(directory) / 'oracle.cpp'
        cpp.write_text(source)
        binary = pathlib.Path(directory) / 'oracle'
        subprocess.run(['c++', '-std=c++20', '-DFREEINK_UI_COMPONENT_STYLE=0',
                        '-I' + str(SDK / 'include'), str(cpp), str(SDK / 'src/FreeInkUI.cpp'),
                        '-o', str(binary)], check=True)
        return subprocess.check_output([str(binary)])

class Golden(unittest.TestCase):
    def test_input_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / 'input'
            subprocess.run(['c++', '-std=c++20', str(HERE / 'input_oracle.cpp'), '-o', str(binary)], check=True)
            self.assertEqual(subprocess.check_output([str(binary)]), (HERE / 'input.golden').read_bytes())

    def test_progress_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / 'progress'
            subprocess.run(['c++', '-std=c++20', str(HERE / 'progress_oracle.cpp'), '-o', str(binary)], check=True)
            self.assertEqual(subprocess.check_output([str(binary)]), (HERE / 'progress.golden').read_bytes())

    def test_status_widgets(self):
        self.assertEqual(record(), gzip.decompress((HERE / 'status.golden.gz').read_bytes()))
    def test_progress_height_mutation(self):
        self.assertNotEqual(record(True), gzip.decompress((HERE / 'status.golden.gz').read_bytes()))

if __name__ == '__main__':
    unittest.main()
