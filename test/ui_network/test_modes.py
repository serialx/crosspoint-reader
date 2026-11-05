"""Mode-list SDK geometry, frozen before the Rust migration."""
from pathlib import Path
import subprocess
import tempfile
import unittest
HERE = Path(__file__).resolve().parent
SDK = HERE.parents[1]/'freeink-sdk/libs/ui/FreeInkUI'

def record(mutate=False):
    source = (HERE/'modes_geometry.cpp').read_text().replace('../ui_settings/Recorder.h', str(HERE.parent/'ui_settings/Recorder.h'))
    methods = (HERE/'modes_original.inc').read_text()
    if mutate:
        methods = methods.replace('props.subtitleText.maxLines = 2;', 'props.subtitleText.maxLines = 1;')
    source = source.replace('#include "modes_original.inc"', methods)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        cpp = root/'oracle.cpp'
        cpp.write_text(source)
        subprocess.run(['c++', '-std=c++20', '-DFREEINK_UI_COMPONENT_STYLE=0', '-I'+str(SDK/'include'),
                        str(cpp), str(SDK/'src/FreeInkUI.cpp'), '-o', str(root/'oracle')], check=True)
        return subprocess.check_output([str(root/'oracle')])

class Modes(unittest.TestCase):
    def test_sdk_geometry(self):
        self.assertEqual(record(), (HERE/'modes_geometry.golden').read_bytes())
    def test_wrapped_subtitle_mutation(self):
        self.assertNotEqual(record(True), (HERE/'modes_geometry.golden').read_bytes())

if __name__ == '__main__':
    unittest.main()
