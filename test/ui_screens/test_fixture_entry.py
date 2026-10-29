"""Fixture selectors must be bounded and staged as part of the recording contract."""
import subprocess
import tempfile
import unittest
from pathlib import Path

from fixtures import prepare


class FixtureEntryTests(unittest.TestCase):
    def test_selector_validation_and_frontlight_state(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / 'fixture-test'
            source = Path(__file__).parent / 'firmware/FixtureTest.cpp'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source),
                            '-o', str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)

    def test_entry_assets_and_rejected_inputs(self):
        for name, expected in [('details-plain', b'details-plain'), ('details-cover', b'details-cover'),
                               ('unknown', b'unknown'), ('oversized', b'x' * 32)]:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                assets = prepare(Path(temporary), 'entry-' + name)
                self.assertEqual(assets[0][0].read_bytes(), expected)
                self.assertEqual(assets[0][1], '/.crosspoint')
                if name == 'details-cover':
                    self.assertEqual(assets[1][0].name, 'OracleCover.png')
                    self.assertEqual(assets[1][0].read_bytes()[:8], b'\x89PNG\r\n\x1a\n')
                    self.assertEqual(assets[1][1], '/')
                else:
                    self.assertEqual(len(assets), 1)

    def test_undeclared_fixture_is_not_silently_staged(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, 'Unknown activity fixture'):
                prepare(Path(temporary), 'entry-typo')
