#!/usr/bin/env python3
"""Fail closed if chapter instrumentation loses a required operation."""
import importlib.util
import unittest
from pathlib import Path

from instrument_chapter import instrument
from run import compare

ROOT = Path(__file__).resolve().parents[2] / 'lib/Epub/Epub/parsers'
SOURCE = (ROOT / 'ChapterPlatform.cpp').read_text()
REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('reader_observer', REPO / 'test/reader_section/instrument.py')
READER_OBSERVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(READER_OBSERVER)


class ChapterTraceTest(unittest.TestCase):
    def test_original_contains_every_anchor(self):
        traced = instrument(SOURCE)
        for op in ('Text', 'Image', 'TableColumns', 'ListMarker', 'RubyGroup', 'RubyAppend',
                   'Step', 'Finish', 'StopRequested'):
            self.assertIn('"' + op + '"', traced)
        for op in ("addWord", "size", "paragraphExists", "createParagraph"):
            self.assertNotIn('"Paragraph.' + op + '"', traced)
        self.assertIn('ChapterTrace::stream.write', traced)
        self.assertIn('LayoutTrace::stream.write', traced)
        assembly = ROOT.parents[3] / 'rust/crates/xp-core/src/chapter/assembly'
        rust = ''.join(path.read_text() for path in assembly.glob('*.rs'))
        for op in ('BeginBlock','EndBlock','BoxOpen','BoxClose','Page','ImageReply','TableRowEnd','getFontAscenderSize','getLineHeight'):
            self.assertIn('"' + op + '"', rust)

    def test_missing_anchor_rejects(self):
        source = SOURCE.replace('[](void*, XpChapterSpan record) { (void)record; }', '')
        with self.assertRaises(ValueError):
            instrument(source)

    def test_duplicate_anchor_rejects(self):
        source = SOURCE + '\n[](void*, XpChapterSpan record) { (void)record; }\n'
        with self.assertRaises(ValueError):
            instrument(source)

    def test_changed_reply_rejects(self):
        with self.assertRaises(ValueError):
            compare({'chapter.ops': b'Reply\t42\n'}, {'chapter.ops': b'Reply\t43\n'})

    def test_missing_operation_rejects(self):
        with self.assertRaises(ValueError):
            compare({'chapter.ops': b'Text\t6162\nEndBlock\t1\n'},
                    {'chapter.ops': b'EndBlock\t1\n'})

    def test_reader_observer_rejects_missing_or_duplicate_sites(self):
        sites = {
            'services.rs': ('    fn trace(&self, _record: &Trace) {}',
                            '    fn trace_bytes(&self, _bytes: &[u8]) {}'),
            'driver.rs': ('    pub(super) fn begin(&mut self, options: &Options) -> bool {',
                          '    pub(super) fn step(&mut self) -> Step {',
                          '    pub(super) fn finish(&mut self) -> bool {'),
            'mod.rs': ('mod driver;',),
        }
        for name, anchors in sites.items():
            relative = READER_OBSERVER.READER / name
            source = (REPO / 'rust' / relative).read_text()
            READER_OBSERVER.instrument(relative, source)
            for anchor in anchors:
                for changed in (source.replace(anchor, ''), source + '\n' + anchor):
                    with self.subTest(file=name, anchor=anchor):
                        with self.assertRaises(ValueError):
                            READER_OBSERVER.instrument(relative, changed)


if __name__ == '__main__':
    unittest.main()
