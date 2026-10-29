"""Routed BLE regression: production C++ bridge methods and real Rust list ABI.

The frozen baseline methods come from bcdfb338. Hardware edges and radio side
 effects are test doubles; long-press consumption uses MappedInputManager code.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def method(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def source(path):
    return (ROOT / path).read_text()


def run(archive, baseline=False, mutate=False):
    harness = (HERE / "ble_bridge.cpp").read_text()
    mapped = source("src/MappedInputManager.cpp")
    code = "\n".join(method(mapped, s) for s in [
        "bool MappedInputManager::wasLongPressed(",
        "void MappedInputManager::suppressNextRelease(",
        "bool MappedInputManager::consumeSuppressedRelease("])
    if baseline:
        code += (HERE / "ble_bridge_original.inc").read_text()
    else:
        settings = source("src/activities/settings/SettingsUiActivity.cpp")
        for signature in ["void SettingsUiActivity::loop()", "XpUiEffect SettingsUiActivity::updateRust(",
                          "void SettingsUiActivity::applyRustEffect(", "XpUiInterest SettingsUiActivity::rustInterest("]:
            code += method(settings, signature) + "\n"
        adapter = source("src/activities/RustActivityAdapter.cpp")
        code += method(adapter, "XpUiInput RustActivityAdapter::sampleRustInput() const") + "\n"
        code += method(adapter, "void RustActivityAdapter::loop()") + "\n"
        ble = method(source("src/activities/settings/BleSettingsActivity.cpp"), "void BleSettingsActivity::loop()")
        code += ble.split("  if (!xp_ui_settings_ble_poll", 1)[0] + "}\n"
    if mutate:
        code = code.replace("view == View::Mapping && ", "")
    harness = harness.replace("// METHODS", code)
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / "bridge.cpp"
        binary = Path(directory) / "bridge"
        cpp.write_text(harness)
        subprocess.run(["c++", "-std=c++20", "-DBASELINE=" + str(int(baseline)),
                        "-I", str(ROOT / "rust/crates/xp-ffi/include"), str(cpp),
                        str(ROOT / "test/cmake/xp_ffi_host.cpp"), archive, "-o", str(binary)], check=True)
        return subprocess.check_output([str(binary)], text=True)


if __name__ == "__main__":
    baseline = run(sys.argv[1], True)
    expected = (HERE / "ble_bridge.golden").read_text()
    assert baseline == expected, (baseline, expected)
    assert run(sys.argv[1], True, True) != expected
    candidate = run(sys.argv[1])
    assert candidate == expected, (candidate, expected)
    print("BLE bridge: original and candidate agree on all routed transitions")
