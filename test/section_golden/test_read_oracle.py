#!/usr/bin/env python3
"""Guard the mutation sampler and the accepted exception rules."""
from copy import deepcopy
import io
import json
from pathlib import Path
import struct
import tarfile
import tempfile
import unittest
from unittest.mock import patch

from read_oracle import HERE, apply_exceptions, record_boundaries, truncation_sizes, mutation_labels, pack_mutations, unpack_mutations
from test_codec_coverage import fixture


class MutationRules(unittest.TestCase):
    def setUp(self):
        with tarfile.open(HERE / 'goldens/sections.tar.gz') as archive:
            member = next(m for m in archive if m.isfile())
            self.name = member.name
            self.data = archive.extractfile(member).read()

    def apply(self, expected, cases):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'exceptions.json'
            path.write_text(json.dumps({'cases': cases}))
            return apply_exceptions(deepcopy(expected), path)

    def test_every_header_byte_and_record_edge_is_sampled(self):
        data, _ = fixture()
        cuts = set(truncation_sizes(data))
        self.assertTrue(set(range(65)) <= cuts)
        self.assertTrue(set(range(0, len(data), 64)) <= cuts)
        for pos in record_boundaries(data):
            for cut in (pos - 1, pos, pos + 1):
                if 0 <= cut < len(data):
                    self.assertIn(cut, cuts)
        # Synthetic drop cap: count(2), tag(1), metadata(12), UTF-8 text(6).
        self.assertTrue({44, 46, 47, 59, 65, 67, 69} <= record_boundaries(data))

    def test_header_exception_proves_count_and_retention(self):
        for cut in range(22, 44):
            count = int.from_bytes(self.data[22:cut][:2].ljust(2, b'\0'), 'little')
            mutation = f'truncate:{cut}'
            expected = {self.name: {mutation: {'exit': 0, 'results': [f'section\t1\t{count}', 'retained\t1']}}}
            case = {'class': 'truncated-header', 'section': self.name, 'mutation': mutation}
            result = self.apply(expected, [case])
            self.assertEqual(result[self.name][mutation]['results'], ['section\t0\t0', 'retained\t0'])
            expected[self.name][mutation]['results'][0] = 'section\t1\t999'
            with self.assertRaises(ValueError):
                self.apply(expected, [case])

    def test_compact_fixture_is_lossless_and_rejects_changed_sampling(self):
        data, _ = fixture()
        labels = mutation_labels(data)
        records = {'synthetic/0/0.bin': {label: {'exit': 0, 'results': ['section\t1\t4']}
                                        for label in labels}}
        # Keep one distinct outcome between equal runs.
        records['synthetic/0/0.bin'][labels[10]] = {'exit': 0, 'results': ['section\t0\t0']}
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'goldens').mkdir()
            with tarfile.open(root / 'goldens/sections.tar.gz', 'w:gz') as archive:
                info = tarfile.TarInfo('synthetic/0/0.bin')
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
            with patch('read_oracle.HERE', root):
                packed = pack_mutations(records)
                decoded = unpack_mutations(packed)
                self.assertEqual(decoded, records)
                decoded['synthetic/0/0.bin'][labels[0]]['results'][0] = 'changed'
                self.assertEqual(decoded['synthetic/0/0.bin'][labels[1]], records['synthetic/0/0.bin'][labels[1]])
                broken = deepcopy(packed)
                broken['sections']['synthetic/0/0.bin']['runs'][0][0] = 0
                with self.assertRaises(ValueError):
                    unpack_mutations(broken)
                packed['sections']['synthetic/0/0.bin']['labels_sha256'] = 'wrong'
                with self.assertRaises(ValueError):
                    unpack_mutations(packed)

    def test_invalid_exception_scope_and_duplicates_fail(self):
        for cut in (21, 44):
            mutation = f'truncate:{cut}'
            expected = {self.name: {mutation: {'exit': 0, 'results': ['section\t1\t0', 'retained\t1']}}}
            case = {'class': 'truncated-header', 'section': self.name, 'mutation': mutation}
            with self.assertRaises(ValueError):
                self.apply(expected, [case])
        lut = struct.unpack_from('<I', self.data, 24)[0]
        expected = {self.name: {f'truncate:{lut + 3}': {'exit': 0, 'results': ['page\t0\t1']}}}
        case = {'class': 'short-lut', 'section': self.name, 'mutation': f'truncate:{lut + 3}', 'rejected_page': 0}
        self.assertEqual(self.apply(expected, [case])[self.name][case['mutation']]['results'], ['page\t0\t0'])
        with self.assertRaises(ValueError):
            self.apply(expected, [case, case])
        case['mutation'] = f'truncate:{lut + 4}'
        expected[self.name][case['mutation']] = {'exit': 0, 'results': ['page\t0\t1']}
        with self.assertRaises(ValueError):
            self.apply(expected, [case])


if __name__ == '__main__':
    unittest.main()
