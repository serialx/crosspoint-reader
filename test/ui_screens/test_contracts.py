"""Regression checks for startup, recording contracts, relocation and selection."""
import copy
import json
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image
from assemble import assemble
from capture import HERE, save, sha, summarize
from compare import compare
from contracts import expected_contract, executable_identity, input_files, validate_recording
from fixtures import prepare


class ContractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        self.suite = json.loads((HERE / 'screens.json').read_text())
        self.group = next(g for g in self.suite['groups'] if g['id'] == 'sleep-custom-image')
        self.contract = expected_contract(self.suite, self.group, 'x3', 0, 0)
        self.case = self.source / 'x3-o0-d0-sleep-custom-image'
        (self.case / 'config').mkdir(parents=True)
        for name, contents in self.contract['configs'].items():
            save(self.case / 'config' / name, contents)
        assets = prepare(self.case, self.contract['fixture'])
        (self.case / 'steps.txt').write_text('\n'.join(self.contract['steps']) + '\n')
        puts = [(p, '/.crosspoint') for p in sorted((self.case / 'config').iterdir())] + assets
        cmd = ['eink-qemu', '--out', str(self.case / 'run'), '--input-quiet', '200']
        for path, target in puts:
            cmd += ['--put', str(path) + '=' + target]
        (self.case / 'run').mkdir()
        start = {'card': {'staged': [{'host': str(p), 'card_dir': t} for p, t in puts]},
                 'pacing': {'input_quiet_ms': 200, 'key_hold_ms': 150, 'panel_idle_ms': 300, 'settle_ms': 200}}
        (self.case / 'run/run.jsonl').write_text(json.dumps(start) + '\n')
        self.run = {'name': self.case.name, 'machine': 'x3', 'orientation': 0, 'dark': 0,
                    'group': self.group['id'], 'command': cmd, 'exit_code': 0, 'host_seconds': 1,
                    'artifact_dir': self.case.name, 'contract': self.contract,
                    'input_sha256': input_files(self.case), 'script_sha256': sha(self.case / 'steps.txt')}

    def test_valid_contract(self):
        validate_recording(self.case, self.run, self.contract)

    def reject_assembly(self):
        meta = {'schema': 2, 'status': 'passed', 'executable_identity': {}, 'artifacts': {}, 'rtc': 'fixed',
                'sleep': 'off', 'turbo': False, 'ghosting': False, 'input_quiet_ms': 200,
                'xtc_fixture_sha256': sha(HERE.parent / 'xtc_diff/generate.py'),
                'harness_sha256': {n: sha(HERE / n) for n in ['fixtures.py', 'fault.py']},
                'host_seconds': 1, 'runs': [self.run]}
        save(self.source / 'manifest.json', meta)
        out = self.root / 'assembled'
        cases = {self.run['name']: (self.group, 'x3', 0, 0)}
        with patch('assemble.declared_cases', return_value=cases), self.assertRaises(ValueError):
            assemble([self.source], out)
        self.assertFalse(out.exists())

    def test_empty_custom_sleep_fixture_rejected_by_assembly(self):
        self.run['contract'] = dict(self.contract, fixture='empty')
        shutil.rmtree(self.case / 'assets')
        prepare(self.case, 'empty')
        self.run['input_sha256'] = input_files(self.case)
        self.reject_assembly()

    def test_legacy_empty_fixture_rejected(self):
        shutil.rmtree(self.case / 'assets')
        prepare(self.case, 'empty')
        self.run['input_sha256'] = input_files(self.case)
        with self.assertRaisesRegex(ValueError, 'inventory'):
            validate_recording(self.case, self.run, self.contract, legacy=True)

    def test_extra_config_rejected(self):
        save(self.case / 'config/extra.json', {})
        self.run['input_sha256'] = input_files(self.case)
        self.reject_assembly()

    def test_removed_config_rejected(self):
        (self.case / 'config/state.json').unlink()
        self.run['input_sha256'] = input_files(self.case)
        self.reject_assembly()

    def test_changed_destination_rejected(self):
        self.run['command'][-1] += 'wrong-directory'
        self.reject_assembly()

    def test_recorded_destination_rejected(self):
        path = self.case / 'run/run.jsonl'
        start = json.loads(path.read_text())
        start['card']['staged'][-1]['card_dir'] = '/wrong-directory'
        save(path, start)
        self.reject_assembly()

    def test_dropped_press_rejected(self):
        group = next(g for g in self.suite['groups'] if g['id'] == 'reader')
        contract = expected_contract(self.suite, group, 'x3', 0, 0)
        steps = contract['steps'].copy()
        # This confirm opens the reader menu after the reader page settles.
        index = steps.index('mark:before-reader-menu')
        steps.pop(steps.index('press:confirm', index))
        with tempfile.TemporaryDirectory() as tmp:
            case = Path(tmp)
            (case / 'config').mkdir()
            for name, value in contract['configs'].items():
                save(case / 'config' / name, value)
            prepare(case, contract['fixture'])
            (case / 'steps.txt').write_text('\n'.join(steps) + '\n')
            run = dict(self.run, contract=contract, input_sha256=input_files(case), script_sha256=sha(case / 'steps.txt'))
            with self.assertRaisesRegex(ValueError, 'Navigation'):
                validate_recording(case, run, contract)

    def test_relocated_contract(self):
        target = self.root / 'moved'
        shutil.copytree(self.case, target)
        shutil.rmtree(self.source)
        validate_recording(target, self.run, self.contract)

    def test_changed_pacing_rejected(self):
        path = self.case / 'run/run.jsonl'
        start = json.loads(path.read_text())
        start['pacing']['key_hold_ms'] = 10
        save(path, start)
        self.reject_assembly()


class SelectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.selection = {'machines': ['x3'], 'orientations': [0], 'dark': [0], 'groups': ['home-theme-0']}
        self.a, self.b = self.root / 'a', self.root / 'b'
        for root in [self.a, self.b]:
            root.mkdir()
            Image.new('RGB', (3, 2), 'white').save(root / 'home.png')
            self.screen = {'id': 'home-theme-0', 'image': 'home.png', 'png_sha256': sha(root / 'home.png'),
                           'activity_expected': 'Home', 'activity_observed': 'Home', 'panel_refreshes': 0,
                           'refresh_requests': [], 'controller_refreshes': [],
                           'startup': {'refresh_requests': ['fast'], 'controller_refreshes': ['wave'], 'panel_refreshes': 4}}
            self.meta = {'schema': 2, 'status': 'passed', 'executable_identity': {'cli_sha256': 'one', 'qemu_sha256': 'two'},
                         'runs': [{'name': 'x3-o0-d0-home-theme-0', 'exit_code': 0, 'screens': [self.screen]}]}
            second = dict(self.screen, id='home-theme-0-next')
            second.pop('startup')
            self.meta['runs'][0]['screens'].append(second)
            save(root / 'manifest.json', self.meta)

    def test_selected_match_and_unselected_difference(self):
        extra = copy.deepcopy(self.meta['runs'][0]); extra['name'] = 'x4-o0-d0-home-theme-0'
        self.meta['runs'].append(extra)
        save(self.a / 'manifest.json', self.meta)
        self.assertTrue(compare(self.a, self.b, self.selection)['passed'])
        with self.assertRaisesRegex(ValueError, 'Screen sets differ'):
            compare(self.a, self.b)

    def test_missing_selected_case(self):
        self.meta['runs'] = []
        save(self.b / 'manifest.json', self.meta)
        with self.assertRaisesRegex(ValueError, 'Incomplete selected'):
            compare(self.a, self.b, self.selection)

    def test_unknown_selection(self):
        with self.assertRaisesRegex(ValueError, 'Invalid selection'):
            compare(self.a, self.b, {'groups': ['typo']})

    def test_startup_home_mode_change(self):
        self.screen['startup']['refresh_requests'] = ['full']
        save(self.b / 'manifest.json', self.meta)
        self.assertFalse(compare(self.a, self.b)['passed'])

    def test_startup_count_change(self):
        self.screen['startup']['panel_refreshes'] += 1
        save(self.b / 'manifest.json', self.meta)
        self.assertFalse(compare(self.a, self.b)['passed'])

    def test_startup_controller_change(self):
        self.screen['startup']['controller_refreshes'] = ['other']
        save(self.b / 'manifest.json', self.meta)
        self.assertFalse(compare(self.a, self.b)['passed'])

    def test_missing_selected_screen(self):
        self.meta['runs'][0]['screens'].pop()
        save(self.b / 'manifest.json', self.meta)
        with self.assertRaisesRegex(ValueError, 'Incomplete selected'):
            compare(self.a, self.b, self.selection)

    def test_missing_startup(self):
        del self.screen['startup']
        save(self.b / 'manifest.json', self.meta)
        with self.assertRaisesRegex(ValueError, 'Missing startup'):
            compare(self.a, self.b)

    def test_executable_location_is_provenance(self):
        self.meta['emulator'] = {'cli': '/different/path', 'qemu': '/another/path'}
        save(self.b / 'manifest.json', self.meta)
        self.assertTrue(compare(self.a, self.b)['passed'])

    def test_executable_digest_must_match(self):
        self.meta['executable_identity']['cli_sha256'] = 'different'
        save(self.b / 'manifest.json', self.meta)
        with self.assertRaisesRegex(ValueError, 'executable_identity'):
            compare(self.a, self.b)

    def test_identity_excludes_paths(self):
        identity = {'cli': '/one', 'qemu': '/two', 'cli_sha256': 'a', 'qemu_sha256': 'b',
                    'cli_version': '1', 'qemu_version': '2'}
        moved = dict(identity, cli='/moved/one', qemu='/moved/two')
        self.assertEqual(executable_identity(identity), executable_identity(moved))


if __name__ == '__main__':
    unittest.main()
