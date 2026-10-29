"""Frozen text-settings widget geometry from dc21f50f and mutation sensitivity."""
from pathlib import Path
import subprocess
import tempfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from rust_host import rust_link_flags
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def record(source):
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / "layout.cpp"
        executable = Path(directory) / "layout"
        cpp.write_text(source)
        subprocess.run([
            "c++", "-std=c++17", "-DFREEINK_DEVICE_X3=1", "-DFREEINK_DEVICE_X4=1",
            "-DFREEINK_CAP_TOUCH=1", "-I", str(ROOT / "src"),
            "-I", str(ROOT / "freeink-sdk/libs/hardware/XteinkDetect/test/host/stubs"),
            "-I", str(ROOT / "freeink-sdk/libs/hardware/BoardConfig/include"), "-I",
            str(ROOT / "freeink-sdk/libs/ui/FreeInkUI/include"),
            "-I", str(ROOT / "lib/EpdFont"), "-I", str(ROOT / "lib/Utf8"),
            str(ROOT / "freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp"),
            str(ROOT / "lib/EpdFont/EpdFont.cpp"), str(ROOT / "lib/Utf8/Utf8.cpp"), *rust_link_flags(),
            str(cpp), "-o", str(executable),
        ], check=True)
        return subprocess.check_output([str(executable)])

if __name__ == "__main__":
    source = (HERE / "Oracle.cpp").read_text()
    actual = record(source)
    if "--record" in sys.argv:
        (HERE / "layout.golden").write_bytes(actual)
    else:
        assert actual == (HERE / "layout.golden").read_bytes(), "text-settings layout changed"
        mutation = source.replace("tabProps.tabInset = fui::Insets{4, 4, 7, 4};", "tabProps.tabInset = fui::Insets{4, 5, 7, 4};")
        assert mutation != source
        assert record(mutation) != actual, "tab inset mutation was not detected"
    print(f"{len(actual.splitlines())} text-settings widget cases passed")
    with tempfile.TemporaryDirectory() as directory:
        binary = Path(directory) / "nav"
        subprocess.run(["c++", "-std=c++17", str(HERE / "NavigationOracle.cpp"), "-o", str(binary)], check=True)
        assert subprocess.check_output([str(binary)]) == (HERE / "navigation.golden").read_bytes()
        changed = (HERE / "NavigationOracle.cpp").read_text().replace("uint32_t(input.now-last)>500", "uint32_t(input.now-last)>=500")
        assert changed != (HERE / "NavigationOracle.cpp").read_text()
        mutant = Path(directory) / "mutation.cpp"
        mutant.write_text(changed)
        subprocess.run(["c++", "-std=c++17", str(mutant), "-o", str(binary)], check=True)
        assert subprocess.check_output([str(binary)]) != (HERE / "navigation.golden").read_bytes()
