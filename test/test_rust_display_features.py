import importlib.util
from pathlib import Path
import unittest
import shutil
import tempfile
import subprocess

spec = importlib.util.spec_from_file_location(
    "rust_display_features", Path(__file__).resolve().parents[1] / "scripts/rust_display_features.py")
selection = importlib.util.module_from_spec(spec)
spec.loader.exec_module(selection)


class DisplayFeaturesTest(unittest.TestCase):
    header = Path(__file__).resolve().parents[1] / "freeink-sdk/libs/hardware/BoardConfig/include/PanelDrivers.h"

    def features(self, defines, header=None):
        return selection.display_features(defines, shutil.which("c++"), header or self.header)

    def select(self, *boards, extra=()):
        return self.features([("FREEINK_RUST_PANEL", 1), *boards, *extra])

    def test_disabled(self):
        self.assertEqual(self.features(["FREEINK_DEVICE_X3"]), [])
        self.assertEqual(self.features([
            ("FREEINK_RUST_PANEL", 0), "FREEINK_DEVICE_X3"]), [])

    def test_sdk_policy_is_authoritative(self):
        # Simulate an SDK-only change; Cargo must select the newly enabled driver.
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / "PanelDrivers.h"
            header.write_text(self.header.read_text() +
                              "\n#undef FREEINK_DRIVER_SSD2677\n#define FREEINK_DRIVER_SSD2677 1\n")
            self.assertEqual(self.features(["FREEINK_RUST_PANEL", "FREEINK_DEVICE_X3"], header), [
                "display-ssd2677", "display-uc8253", "display-uc8279-x3"])

    def test_preprocess_failure_is_fatal(self):
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / "PanelDrivers.h"
            header.write_text("#error invalid panel policy\n")
            with self.assertRaises(subprocess.CalledProcessError):
                self.features(["FREEINK_RUST_PANEL", "FREEINK_DEVICE_X3"], header)

    def test_other_sdk_boards(self):
        for board in ("DELINK", "MURPHY_M4", "ONEPAGE", "WS397"):
            self.assertEqual(self.select("FREEINK_DEVICE_" + board), ["display-ssd1677"])
        for board in ("M5", "MURPHY", "LILYGO", "PAPERS3", "M5PAPER"):
            self.assertEqual(self.select("FREEINK_DEVICE_" + board), [])

    def test_c3(self):
        self.assertEqual(self.select("FREEINK_DEVICE_X3", "FREEINK_DEVICE_X4"), [
            "display-ssd1677", "display-uc8179", "display-uc8253",
            "display-uc8279-x3", "display-uc8279-x4"])

    def test_s3_boards(self):
        for board in ("X4PRO", "X4CLASSIC"):
            self.assertEqual(self.select("FREEINK_DEVICE_" + board), [
                "display-ssd1677", "display-uc8179", "display-uc8279-x4"])
        for board in ("STICKY", "PICCO"):
            self.assertEqual(self.select("FREEINK_DEVICE_" + board), [
                "display-ssd1677", "display-ssd2677"])
        for board, driver in (("METALIO_EINK4", "ssd1677"),
                              ("PAPERMONO", "paper-mono"), ("EEGO_A4", "uc8279c-a4")):
            self.assertEqual(self.select("FREEINK_DEVICE_" + board), ["display-" + driver])

    def test_disabled_board_and_cpp_overrides(self):
        self.assertEqual(self.select(("FREEINK_DEVICE_X3", 0)), [])
        for flag in ("FREEINK_SSD1677_CONFIG", "SSD1677_PROBE_DEBUG"):
            self.assertEqual(self.select("FREEINK_DEVICE_STICKY", extra=[flag]), ["display-ssd2677"])


if __name__ == "__main__":
    unittest.main()
