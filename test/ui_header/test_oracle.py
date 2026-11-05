"""Compare shared header service calls and hit records against the pinned SDK."""
import gzip
import pathlib
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SDK = ROOT / "freeink-sdk/libs/ui/FreeInkUI"


def record(directory, baseline=False, mutation=False):
    source = (HERE / "oracle.cpp").read_text()
    if mutation:
        source = source.replace("props.sidePadding = 12;", "props.sidePadding = 13;")
    cpp = directory / "oracle.cpp"
    cpp.write_text(source)
    binary = directory / "oracle"
    command = ["c++", "-std=c++20", "-DFREEINK_UI_COMPONENT_STYLE=0",
               "-I" + str(SDK / "include"), "-I" + str(ROOT / "src"), str(cpp),
               str(SDK / "src/FreeInkUI.cpp"), "-o", str(binary)]
    if baseline:
        command.append("-DHEADER_BASELINE")
    else:
        command.append(str(ROOT / "src/components/UiHeader.cpp"))
    subprocess.run(command, check=True)
    return subprocess.check_output([str(binary)])


class HeaderCalls(unittest.TestCase):
    def test_shared_calls_and_hits(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            baseline = record(directory, baseline=True)
            self.assertEqual(baseline.count(b"case "), 512)
            self.assertEqual(baseline, gzip.decompress((HERE / "calls.golden.gz").read_bytes()))
            self.assertEqual(record(directory), baseline)
            self.assertNotEqual(record(directory, mutation=True), baseline)


if __name__ == "__main__":
    unittest.main()
