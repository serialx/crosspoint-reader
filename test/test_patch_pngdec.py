import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts' / 'patch_pngdec.py'
SPEC = importlib.util.spec_from_file_location('patch_pngdec', SCRIPT)
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class Environment:
    def __init__(self, root):
        self.root = root

    def subst(self, value):
        return {'$PROJECT_LIBDEPS_DIR': str(self.root), '$PIOENV': 'active'}[value]


class PatchPngdecTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def library(self, name='active'):
        library = self.root / name / 'PNGdec'
        source = library / 'src'
        source.mkdir(parents=True)
        (source / 'PNGdec.h').write_text(PATCH.HEADER_OLD + '\n' + PATCH.ARENA_OLD)
        (source / 'png.inl').write_text('\n'.join((
            '#include "zlib.h"', PATCH.INIT_OLD, '    struct inflate_state *state;',
            'z_stream d_stream;', 'crc32(0, &s[12], len+4)',
            'inflate(&d_stream, Z_NO_FLUSH,', 'inflateEnd(&d_stream)',
            'd_stream.avail_in d_stream.avail_out d_stream.next_in d_stream.next_out',
            'Z_OK Z_STREAM_END Z_DATA_ERROR Z_STREAM_ERROR Z_BUF_ERROR')))
        for filename in PATCH.ZLIB_SOURCES:
            (source / filename).write_text(filename)
        return library

    def snapshot(self, library):
        return {p.name: p.read_bytes() for p in (library / 'src').iterdir()}

    def test_active_environment_idempotence_and_source_exclusion(self):
        active, sibling = self.library(), self.library('sibling')
        untouched = self.snapshot(sibling)
        self.assertTrue(PATCH.patch_pngdec(Environment(self.root), True))
        first = self.snapshot(active)
        self.assertTrue(PATCH.patch_pngdec(Environment(self.root), True))
        self.assertEqual(first, self.snapshot(active))
        self.assertEqual(untouched, self.snapshot(sibling))
        self.assertFalse(list((active / 'src').glob('*.c')))
        for filename in PATCH.ZLIB_SOURCES:
            self.assertEqual((active / 'src' / (filename + '.disabled')).read_text(), filename)
        self.assertIn(PATCH.HEADER_NEW, first['PNGdec.h'].decode())
        self.assertIn(PATCH.INIT_NEW, first['png.inl'].decode())
        self.assertIn('d_stream.stream.avail_out', first['png.inl'].decode())

    def test_unknown_source_is_transactional(self):
        library = self.library()
        (library / 'src' / 'png.inl').write_text('unknown upstream source')
        before = self.snapshot(library)
        with self.assertRaises(RuntimeError):
            PATCH.patch_library(library)
        self.assertEqual(before, self.snapshot(library))

    def test_missing_source_is_transactional(self):
        library = self.library()
        (library / 'src' / 'inflate.c').unlink()
        before = self.snapshot(library)
        with self.assertRaises(RuntimeError):
            PATCH.patch_library(library)
        self.assertEqual(before, self.snapshot(library))

    def test_missing_active_dependency(self):
        self.library('sibling')
        self.assertFalse(PATCH.patch_pngdec(Environment(self.root)))
        with self.assertRaises(SystemExit):
            PATCH.patch_pngdec(Environment(self.root), True)


if __name__ == '__main__':
    unittest.main()
