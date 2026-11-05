from pathlib import Path
import subprocess
import tempfile
import unittest
HERE=Path(__file__).resolve().parent
SDK=HERE.parents[1]/'freeink-sdk/libs/ui/FreeInkUI'
def record(mutate=False):
    with tempfile.TemporaryDirectory() as directory:
        target=Path(directory)
        source=(HERE/'widgets.cpp').read_text()
        if mutate:source=source.replace('p.buttonHeight = 40;', 'p.buttonHeight = 41;')
        (target/'widgets.cpp').write_text(source)
        subprocess.run(['c++','-std=c++20','-DFREEINK_UI_COMPONENT_STYLE=0','-I'+str(HERE),'-I'+str(SDK/'include'),str(target/'widgets.cpp'),str(SDK/'src/FreeInkUI.cpp'),'-o',str(target/'widgets')],check=True)
        return subprocess.check_output([str(target/'widgets')])
class Widgets(unittest.TestCase):
    def test_original(self):self.assertEqual(record(),(HERE/'widgets.golden').read_bytes())
    def test_mutation(self):self.assertNotEqual(record(True),(HERE/'widgets.golden').read_bytes())
if __name__=='__main__':unittest.main()
