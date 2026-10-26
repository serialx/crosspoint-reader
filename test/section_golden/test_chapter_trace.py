#!/usr/bin/env python3
"""Fail closed if chapter instrumentation loses a required operation."""
import unittest
from pathlib import Path

from instrument_chapter import instrument
from run import compare

SOURCE = Path(__file__).resolve().parents[2] / 'lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp'


class ChapterTraceTest(unittest.TestCase):
    def test_original_contains_every_anchor(self):
        traced = instrument(SOURCE.read_text())
        for op in ('BeginBlock', 'EndBlock', 'Text', 'ImageReply', 'TableColumns', 'Footnote',
                   'Paragraph.addWord', 'Paragraph.size', 'getFontAscenderSize'):
            self.assertIn('"' + op + '"', traced)

    def test_missing_anchor_rejects(self):
        source = SOURCE.read_text().replace('  boxScopes.push_back(box);', '')
        with self.assertRaises(ValueError):
            instrument(source)

    def test_duplicate_anchor_rejects(self):
        source = SOURCE.read_text() + '\n  boxScopes.push_back(box);\n'
        with self.assertRaises(ValueError):
            instrument(source)

    def test_changed_reply_rejects(self):
        with self.assertRaises(ValueError):
            compare({'chapter.ops': b'Reply\t42\n'}, {'chapter.ops': b'Reply\t43\n'})

    def test_missing_operation_rejects(self):
        with self.assertRaises(ValueError):
            compare({'chapter.ops': b'Text\t6162\nEndBlock\t1\n'},
                    {'chapter.ops': b'EndBlock\t1\n'})


if __name__ == '__main__':
    unittest.main()
