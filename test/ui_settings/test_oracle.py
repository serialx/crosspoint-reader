"""Frozen SDK settings rows, including wrapped values and disabled controls."""
import gzip
import pathlib
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SDK = ROOT / "freeink-sdk/libs/ui/FreeInkUI"


def record(mutate=False):
    with tempfile.TemporaryDirectory() as directory:
        source = (HERE / "oracle.cpp").read_text()
        source = source.replace('Recorder.h', str(HERE / 'Recorder.h'))
        if mutate:
            source = source.replace('props.valueInset = 8;', 'props.valueInset = 9;')
        cpp = pathlib.Path(directory) / 'oracle.cpp'
        cpp.write_text(source)
        binary = pathlib.Path(directory) / 'oracle'
        subprocess.run(['c++', '-std=c++20', '-DFREEINK_UI_COMPONENT_STYLE=0',
                        '-I' + str(SDK / 'include'), str(cpp),
                        str(SDK / 'src/FreeInkUI.cpp'), '-o', str(binary)], check=True)
        return subprocess.check_output([str(binary)])


class Golden(unittest.TestCase):
    def test_input_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / 'input'
            subprocess.run(['c++', '-std=c++20', str(HERE / 'input_oracle.cpp'), '-o', str(binary)], check=True)
            self.assertEqual(subprocess.check_output([str(binary)]), (HERE / 'input.golden').read_bytes())

    def test_ble_learning(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / 'ble'
            subprocess.run(['c++', '-std=c++20', str(HERE / 'ble_oracle.cpp'), '-o', str(binary)], check=True)
            self.assertEqual(subprocess.check_output([str(binary)]), (HERE / 'ble.golden').read_bytes())

    def test_edit_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / 'edit'
            source = (HERE / 'edit_oracle.cpp').read_text()
            subprocess.run(['c++', '-std=c++20', str(HERE / 'edit_oracle.cpp'), '-o', str(binary)], check=True)
            golden = gzip.decompress((HERE / 'edit.golden.gz').read_bytes())
            self.assertEqual(subprocess.check_output([str(binary)]), golden)
            mutation = pathlib.Path(directory) / 'mutation.cpp'
            mutation.write_text(source.replace('increment > maximum', 'increment >= maximum'))
            subprocess.run(['c++', '-std=c++20', str(mutation), '-o', str(binary)], check=True)
            self.assertNotEqual(subprocess.check_output([str(binary)]), golden)

    def test_settings_widgets(self):
        self.assertEqual(record(), gzip.decompress((HERE / 'widgets.golden.gz').read_bytes()))

    def test_value_inset_mutation(self):
        self.assertNotEqual(record(True), gzip.decompress((HERE / 'widgets.golden.gz').read_bytes()))


if __name__ == '__main__':
    unittest.main()
