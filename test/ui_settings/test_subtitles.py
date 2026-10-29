"""Freeze the pinned SDK's wrapped-subtitle and row-slack rules."""
from pathlib import Path
import subprocess
import tempfile
import unittest
HERE = Path(__file__).resolve().parent
SDK = HERE.parents[1] / 'freeink-sdk/libs/ui/FreeInkUI'

def record(mutate=False):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = (HERE/'subtitles.cpp').read_text()
        if mutate:
            source = source.replace('props.subtitleText.maxLines = lines;', 'props.subtitleText.maxLines = 1;')
        cpp = root/'subtitles.cpp'
        cpp.write_text(source)
        subprocess.run(['c++', '-std=c++20', '-DFREEINK_UI_COMPONENT_STYLE=0', '-I'+str(HERE),
                        '-I'+str(SDK/'include'), str(cpp), str(SDK/'src/FreeInkUI.cpp'), '-o', str(root/'oracle')], check=True)
        return subprocess.check_output([str(root/'oracle')])

class Subtitles(unittest.TestCase):
    def test_sdk_golden(self):
        self.assertEqual(record(), (HERE/'subtitles.golden').read_bytes())
    def test_wrap_mutation(self):
        self.assertNotEqual(record(True), (HERE/'subtitles.golden').read_bytes())

if __name__ == '__main__':
    unittest.main()
