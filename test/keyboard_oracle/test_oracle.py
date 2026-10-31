import gzip
import unittest
import record
import widgets

class Golden(unittest.TestCase):
    def test_transcript(self):
        self.assertEqual(record.record(), b''.join(gzip.decompress(p.read_bytes()) for p in sorted(record.HERE.glob('transcript-*.gz'))))

    def test_cursor_mutation(self):
        self.assertNotEqual(record.record(mutation=True), b''.join(gzip.decompress(p.read_bytes()) for p in sorted(record.HERE.glob('transcript-*.gz'))))

    def test_widgets(self):
        self.assertEqual(widgets.record(), gzip.decompress((record.HERE/'widgets.gz').read_bytes()))

if __name__=='__main__':
    unittest.main()
