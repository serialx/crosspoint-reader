import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "rust_display_features", Path(__file__).resolve().parents[1] / "scripts/rust_display_features.py")
selection = importlib.util.module_from_spec(spec)
spec.loader.exec_module(selection)


class DisplayFeaturesTest(unittest.TestCase):
    def select(self, *boards, extra=()):
        return selection.display_features([("FREEINK_RUST_PANEL", 1), *boards, *extra])

    def test_disabled(self):
        self.assertEqual(selection.display_features(["FREEINK_DEVICE_X3"]), [])
        self.assertEqual(selection.display_features([
            ("FREEINK_RUST_PANEL", 0), "FREEINK_DEVICE_X3"]), [])

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
