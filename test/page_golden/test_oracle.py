#!/usr/bin/env python3
"""Comparator contract: each behavior channel fails independently; counters may improve."""
import copy
import unittest
import run


class Comparator(unittest.TestCase):
    def setUp(self):
        self.value = {'cases': {'book/config': {'pages': {'0/0': {
            'records_sha256': 'record', 'raw_sha256': 'raw', 'wire_sha256': 'wire',
            'pixels': [['0,0,0,0,0', 'pixel']], 'calls': ['10,50,0']}}}}}

    def test_each_observation(self):
        for key in ('records_sha256', 'raw_sha256', 'wire_sha256', 'pixels'):
            with self.subTest(key=key):
                changed = copy.deepcopy(self.value)
                changed['cases']['book/config']['pages']['0/0'][key] = 'changed'
                with self.assertRaisesRegex(ValueError, 'Page golden mismatch'):
                    run.compare(self.value, changed)

    def test_missing_page(self):
        changed = copy.deepcopy(self.value)
        changed['cases']['book/config']['pages'].clear()
        with self.assertRaises(ValueError):
            run.compare(self.value, changed)

    def test_counters_are_diagnostics(self):
        changed = copy.deepcopy(self.value)
        changed['cases']['book/config']['pages']['0/0']['calls'] = ['1,0,0']
        run.compare(self.value, changed)


if __name__ == '__main__':
    unittest.main()
