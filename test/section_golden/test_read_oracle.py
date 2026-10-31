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

from read_oracle import (HERE, HEADER_FIELDS, apply_exceptions, checked_offset_result, header_mutations,
                         record_boundaries, truncation_sizes, mutation_labels, pack_mutations, unpack_mutations)
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

    def test_header_offsets_and_count_are_sampled_in_both_versions(self):
        data, _ = fixture()
        cases = dict(header_mutations(data))
        for kind in ('final', 'partial'):
            size = len(data) + (8 if kind == 'partial' else 0)
            for field, (pos, fmt) in HEADER_FIELDS.items():
                old = struct.unpack_from('<' + fmt, data, pos)[0]
                mask = 0xffff if fmt == 'H' else 0xffffffff
                for value in (0, 1, old - 1, old + 1, 0x7fffffff, 0x80000000,
                              0xfffffffc, 0xfffffffd, 0xfffffffe, 0xffffffff, size - 1, size, size + 1):
                    value &= mask
                    mutated = cases[f'header:{kind}:{field}:{value}']
                    self.assertEqual(struct.unpack_from('<' + fmt, mutated, pos)[0], value)
                    self.assertEqual(mutated[0], 68 if kind == 'final' else 214)
        for value in range(0xfffffffc, 0x100000000):
            mutated = cases[f'header:partial:trailer-wrap:{value}']
            self.assertEqual(struct.unpack_from('<H', mutated, 22)[0], 1)
            self.assertEqual(struct.unpack_from('<II', mutated, 36), (44, value))

    def test_checked_offset_rule_proves_old_values_and_only_changes_wrapped_reads(self):
        data, _ = fixture()
        count = struct.unpack_from('<H', data, 22)[0]
        for kind in ('final', 'partial'):
            mutation = f'header:{kind}:visible:4294967292'
            mutated = dict(header_mutations(data))[mutation]
            lines = [f'section\t1\t{count}']
            for page in range(count):
                old = struct.unpack_from('<I', mutated, (page - 1) * 4)[0] if page else None
                lines += [f'lookup\t{page}\t{old if old is not None else "none"}',
                          f'page\t{page}\t1', f'visible\t{page}\t{old if old is not None else 0}']
            lines.append('retained\t1')
            record = {'exit': 0, 'results': lines}
            result = checked_offset_result(data, mutation, record)
            if kind == 'partial':
                self.assertEqual(result, {'exit': 0, 'results': ['section\t0\t0', 'retained\t0']})
            else:
                self.assertEqual(result['results'][0], lines[0])
                self.assertEqual(result['results'][-1], lines[-1])
                for page in range(count):
                    self.assertIn(f'lookup\t{page}\tnone', result['results'])
                    self.assertIn(f'page\t{page}\t1', result['results'])
                    self.assertIn(f'visible\t{page}\t0', result['results'])
            for index in range(len(lines)):
                broken = deepcopy(record)
                broken['results'][index] = 'wrong'
                with self.assertRaises(ValueError):
                    checked_offset_result(data, mutation, broken)
            for label in (f'header:{kind}:visible:1', f'header:{kind}:page-lut:4294967292'):
                with self.assertRaises(ValueError):
                    checked_offset_result(data, label, record)

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
