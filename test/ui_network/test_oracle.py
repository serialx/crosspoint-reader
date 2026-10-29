"""Frozen network effects and SDK geometry; no network, USB, or device I/O."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import prepare
HERE = Path(__file__).resolve().parent
SDK = HERE.parents[1] / 'freeink-sdk/libs/ui/FreeInkUI'
def run(source, widget=False):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        cpp = root/'oracle.cpp'
        cpp.write_text(source)
        command = ['c++', '-std=c++20', '-pthread', str(cpp), '-o', str(root/'oracle')]
        if widget:
            command += ['-DFREEINK_UI_COMPONENT_STYLE=0', '-I'+str(SDK/'include'), str(SDK/'src/FreeInkUI.cpp')]
        subprocess.run(command, check=True)
        return subprocess.check_output([str(root/'oracle')])
def frozen():
    return (HERE/'effects.cpp').read_text().replace('// PRODUCTION_METHODS', (HERE/'original.inc').read_text()).replace('// PRODUCTION_HELPERS', (HERE/'original_helpers.inc').read_text())
class Oracle(unittest.TestCase):
    def test_frozen_effects(self):
        self.assertEqual(run(frozen()), (HERE/'effects.golden').read_bytes())
    def test_current_effects(self):
        self.assertEqual(run(prepare.generate()), (HERE/'effects.golden').read_bytes())
    def test_usb_order_mutation(self):
        source = frozen()
        changed = source.replace('requestUpdateAndWait();\n  if (!Storage.beginUsbDrive())', 'Storage.beginUsbDrive();\n  requestUpdateAndWait();\n  if (false)')
        self.assertNotEqual(source, changed)
        self.assertNotEqual(run(changed), (HERE/'effects.golden').read_bytes())
    def test_rssi_mutation(self):
        source = frozen()
        changed = source.replace('-85, -75, -65, -55', '-84, -75, -65, -55')
        self.assertNotEqual(source, changed)
        self.assertNotEqual(run(changed), (HERE/'effects.golden').read_bytes())
    def test_timeout_mutation(self):
        source = frozen()
        changed = source.replace('millis() - connectionStartTime > timeoutMs', 'millis() - connectionStartTime >= timeoutMs')
        self.assertNotEqual(source, changed)
        self.assertNotEqual(run(changed), (HERE/'effects.golden').read_bytes())
    def test_widgets(self):
        self.assertEqual(run((HERE/'widgets.cpp').read_text(), True), (HERE/'widgets.golden').read_bytes())
    def test_usb_geometry(self):
        self.assertEqual(run((HERE/'usb_geometry.cpp').read_text(), True), (HERE/'usb_geometry.golden').read_bytes())
    def test_usb_geometry_mutation(self):
        source = (HERE/'usb_geometry.cpp').read_text()
        changed = source.replace('(body.height - totalHeight) / 2', '(body.height - totalHeight) / 3')
        self.assertNotEqual(source, changed)
        self.assertNotEqual(run(changed, True), (HERE/'usb_geometry.golden').read_bytes())
    def test_widget_mutation(self):
        source = (HERE/'widgets.cpp').read_text()
        changed = source.replace('props.valueInset = 8;', 'props.valueInset = 9;')
        self.assertNotEqual(source, changed)
        self.assertNotEqual(run(changed, True), (HERE/'widgets.golden').read_bytes())
if __name__ == '__main__':
    unittest.main()
