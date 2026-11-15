"""A reduced replay must not pass by comparing only the jobs it ran."""
import unittest
from run import require_case_keys


class CaseKeysTest(unittest.TestCase):
    def test_complete_matrix(self):
        require_case_keys({'image/0/0': {}, 'image/0/1': {}}, {'image/0/0': {}, 'image/0/1': {}})

    def test_omitted_job(self):
        with self.assertRaisesRegex(SystemExit, '1 missing'):
            require_case_keys({'image/0/0': {}}, {'image/0/0': {}, 'image/0/1': {}})

    def test_unexpected_job(self):
        with self.assertRaisesRegex(SystemExit, '1 unexpected'):
            require_case_keys({'image/0/0': {}, 'image/0/1': {}}, {'image/0/0': {}})


if __name__ == '__main__':
    unittest.main()
