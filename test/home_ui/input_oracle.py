"""Run the baseline's actual Home input code with deterministic input leaves."""
import gzip
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
BASE = "bcdfb338644ded71a1898a3b7f902feb40c1e6fc"


def original(path):
    return subprocess.check_output(["git", "show", f"{BASE}:{path}"], cwd=ROOT, text=True)


def method(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def record_home(mutation=False):
    with tempfile.TemporaryDirectory() as directory:
        out = Path(directory)
        (out / "MappedInputManager.h").write_text((HERE / "input_leaves.h").read_text())
        (out / "ButtonNavigator.h").write_text(original("src/util/ButtonNavigator.h"))
        home = original("src/activities/home/HomeActivity.cpp")
        header = original("src/activities/home/HomeActivity.h")
        mapped = original("src/MappedInputManager.cpp")
        source = (HERE / "home_input.cpp").read_text()
        for name, result, args in [("menuItemToIndex", "int", "HomeMenuItem item, bool hasOpdsUrl"),
                                   ("indexToMenuItem", "HomeMenuItem", "int idx, bool hasOpdsUrl")]:
            body = method(header, f"static {result} {name}(")
            source += body.replace(f"static {result} {name}", f"{result} HomeActivity::{name}", 1)
        source += method(home, "int HomeActivity::getMenuItemCount()")
        source += method(home, "void HomeActivity::loop()")
        for name in ["rowTouch", "colTouch"]:
            source += method(mapped, f"MappedInputManager::RowTouch MappedInputManager::{name}(")
        source += original("src/util/ButtonNavigator.cpp").replace('#include "ButtonNavigator.h"', '')
        source += (HERE / "home_input_cases.cpp").read_text()
        if mutation:
            source = source.replace("buttonHeldLongEnough = mappedInput->getHeldTime() > continuousStartMs",
                                    "buttonHeldLongEnough = mappedInput->getHeldTime() >= continuousStartMs")
        (out / "input.cpp").write_text(source)
        subprocess.run(["c++", "-std=c++20", "-I" + str(out), str(out / "input.cpp"),
                        "-o", str(out / "input")], check=True)
        return subprocess.check_output([str(out / "input")])


def record_file(mutation=False):
    with tempfile.TemporaryDirectory() as directory:
        out = Path(directory)
        (out / "MappedInputManager.h").write_text((HERE / "input_leaves.h").read_text())
        (out / "ButtonNavigator.h").write_text(original("src/util/ButtonNavigator.h"))
        browser = original("src/activities/home/FileBrowserActivity.cpp")
        base = original("src/activities/UiListActivity.cpp")
        source = (HERE / "file_input.cpp").read_text()
        for signature in ["void FileBrowserActivity::activateSelected()", "bool FileBrowserActivity::handleCustomInput()",
                          "bool FileBrowserActivity::handleButtons()", "size_t FileBrowserActivity::findEntry("]:
            source += method(browser, signature)
        for name in ["navigateButtons", "moveSelectionTo"]:
            source += method(base, f"void UiListActivity::{name}(").replace("UiListActivity::", "FileBrowserActivity::")
        source += original("src/util/ButtonNavigator.cpp").replace('#include "ButtonNavigator.h"', '')
        source += (HERE / "file_input_cases.cpp").read_text()
        if mutation:
            source = source.replace("getHeldTime() >= GO_HOME_MS", "getHeldTime() > GO_HOME_MS")
        (out / "input.cpp").write_text(source)
        sdk = ROOT / "freeink-sdk/libs/ui/FreeInkUI"
        subprocess.run(["c++", "-std=c++20", "-DFREEINK_CAP_TOUCH=0", "-DFREEINK_UI_COMPONENT_STYLE=0",
                        "-I" + str(out), "-I" + str(sdk / "include"), str(out / "input.cpp"),
                        str(sdk / "src/FreeInkUI.cpp"), "-o", str(out / "input")], check=True)
        return subprocess.check_output([str(out / "input")])


class HomeInput(unittest.TestCase):
    def test_native_file_transitions(self):
        self.assertEqual(record_file(), gzip.decompress((HERE / "file-input.golden.gz").read_bytes()))

    def test_root_hold_boundary_mutation(self):
        self.assertNotEqual(record_file(True), gzip.decompress((HERE / "file-input.golden.gz").read_bytes()))

    def test_native_home_transitions(self):
        self.assertEqual(record_home(), gzip.decompress((HERE / "home-input.golden.gz").read_bytes()))

    def test_repeat_boundary_mutation(self):
        self.assertNotEqual(record_home(True), gzip.decompress((HERE / "home-input.golden.gz").read_bytes()))


if __name__ == "__main__":
    unittest.main()
