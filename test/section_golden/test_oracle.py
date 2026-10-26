"""Verify that corpus comparison fails closed when output changes."""

from pathlib import Path
import tempfile
import unittest

from run import compact_layout, compare, read_archive, write_archive


class OracleTest(unittest.TestCase):
    def setUp(self):
        self.sections = {"book/0/0.bin": b"\x44\x00\x01\x02", "book/1/0.bin": b"\x44\x03"}

    def test_archive_preserves_complete_binary_files(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "sections.tar.gz"
            write_archive(path, self.sections)
            original = path.read_bytes()
            compare(self.sections, read_archive(path))
            write_archive(path, dict(reversed(list(self.sections.items()))))
            self.assertEqual(original, path.read_bytes())

    def test_layout_mismatch_identifies_paragraph_and_event(self):
        old = b"paragraph\t480\ntoken\t0\nparagraph\t240\nvisual\t0\t10\n"
        with self.assertRaisesRegex(ValueError, "layout call 2, trace line 4"):
            compare({"book/0/0.layout": old}, {"book/0/0.layout": old.replace(b"10", b"11")})

    def test_paragraph_filter_keeps_other_operations_and_replies(self):
        from run import filter_paragraph_ops
        trace = b"Paragraph.size\nReply\t3\nText\ta\nParagraph.addWord\tb\nPage\t1\nReply\t9\n"
        self.assertEqual(filter_paragraph_ops(trace), b"Text\ta\nPage\t1\nReply\t9\n")

    def test_live_paragraph_service_changes_fail_comparison(self):
        from run import filter_paragraph_ops
        for service in (b"bodySpaceWidth", b"bodyTextAdvance", b"pageWidth", b"tableBytes",
                        b"setTableStacked", b"addPendingFootnote", b"createParagraph",
                        b"resetParagraph", b"applyParagraphDirection"):
            with self.subTest(service=service):
                trace = b"Paragraph." + service + b"\t1\nReply\t9\n"
                self.assertEqual(filter_paragraph_ops(trace), trace)
                for mutation in (trace.replace(b"Reply\t9", b"Reply\t10"),
                                 trace.replace(b"\t1\n", b"\t2\n"), b""):
                    with self.assertRaises(ValueError):
                        compare({"book/0/0.ops": filter_paragraph_ops(trace)},
                                {"book/0/0.ops": filter_paragraph_ops(mutation)})

    def test_instrumentation_fails_closed_when_anchor_is_missing(self):
        from instrument_layout import instrument
        with self.assertRaisesRegex(ValueError, "anchor must occur once"):
            instrument("// No layout source")

    def test_unsampled_layout_change_alters_digest(self):
        full = b"paragraph\t480\ntoken\t0\nparagraph\t480\nvisual\t0\t10\n"
        compact = compact_layout(full)
        self.assertNotIn(b"visual", compact)
        self.assertNotEqual(compact, compact_layout(full.replace(b"10", b"11")))

    def test_rejects_byte_mutation(self):
        changed = dict(self.sections)
        changed["book/0/0.bin"] = b"\x44\x00\xff\x02"
        with self.assertRaisesRegex(ValueError, "first byte offset 2"):
            compare(self.sections, changed)

    def test_rejects_truncation_and_trailing_bytes(self):
        for data in (b"\x44\x00", b"\x44\x00\x01\x02\x03"):
            changed = dict(self.sections)
            changed["book/0/0.bin"] = data
            with self.assertRaisesRegex(ValueError, "Section differs"):
                compare(self.sections, changed)

    def test_rejects_missing_or_extra_cases(self):
        missing = dict(self.sections)
        del missing["book/1/0.bin"]
        extra = dict(self.sections)
        extra["book/2/0.bin"] = b"\x44"
        for changed in (missing, extra):
            with self.assertRaisesRegex(ValueError, "Section set changed"):
                compare(self.sections, changed)


if __name__ == "__main__":
    unittest.main()
