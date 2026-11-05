"""Host checks for comparison failures and deterministic public fixtures."""
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from PIL import Image
from compare import compare
from fixtures import epub
from capture import HERE, steps_for, summarize


class OracleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.old = self.root / 'old'
        self.new = self.root / 'new'
        for path in [self.old, self.new]:
            path.mkdir()
            Image.new('RGB', (3, 2), 'white').save(path / 'screen.png')
            self.write_manifest(path)

    def write_manifest(self, path, **changes):
        screen = {'id': 'home', 'image': 'screen.png', 'png_sha256': hashlib.sha256((path/'screen.png').read_bytes()).hexdigest(),
                  'refresh_requests': ['half', 'fast'], 'panel_refreshes': 2,
                  'startup': {'refresh_requests': ['fast'], 'controller_refreshes': ['wave'], 'panel_refreshes': 1}}
        screen.update(changes)
        data = {'schema': 2, 'status': 'passed', 'suite_sha256': 'suite', 'emulator': 'pinned', 'rtc': 'fixed',
                'sleep': 'off', 'turbo': False, 'ghosting': False,
                'runs': [{'name': 'x3', 'exit_code': 0, 'screens': [screen]}]}
        (path / 'manifest.json').write_text(json.dumps(data))

    def test_identical(self):
        self.assertTrue(compare(self.old, self.new)['passed'])

    def test_one_pixel(self):
        image = Image.open(self.new / 'screen.png').convert('RGB')
        image.putpixel((2, 1), (0, 0, 0))
        image.save(self.new / 'screen.png')
        self.write_manifest(self.new)
        result = compare(self.old, self.new)
        self.assertFalse(result['passed'])
        self.assertEqual(result['results'][0]['different_pixels'], 1)
        self.assertEqual(result['results'][0]['bbox'], (2, 1, 3, 2))

    def test_mode_order(self):
        self.write_manifest(self.new, refresh_requests=['fast', 'half'])
        self.assertFalse(compare(self.old, self.new)['passed'])

    def test_refresh_count(self):
        self.write_manifest(self.new, panel_refreshes=3)
        self.assertFalse(compare(self.old, self.new)['passed'])

    def test_controller_waveform(self):
        self.write_manifest(self.new, controller_refreshes=['different waveform'])
        self.assertFalse(compare(self.old, self.new)['passed'])

    def test_unexpected_activity(self):
        self.write_manifest(self.new, activity_expected='Home', activity_observed='Settings')
        with self.assertRaisesRegex(ValueError, 'Unexpected activity'):
            compare(self.old, self.new)

    def test_source_inventory(self):
        here = Path(__file__).resolve().parent
        repo = here.parents[1]
        coverage = json.loads((here / 'coverage.json').read_text())
        actual = {str(p.relative_to(repo)) for p in (repo / 'src/activities').rglob('*.cpp')}
        declared = [r['source'] for r in coverage['sources']]
        self.assertEqual(actual, set(declared))
        self.assertEqual(len(declared), len(set(declared)))
        groups = {g['id'] for g in json.loads((here / 'screens.json').read_text())['groups']}
        for row in coverage['sources']:
            self.assertTrue(set(row.get('groups', [])).issubset(groups), row['source'])

    def test_dimensions(self):
        Image.new('RGB', (2, 3), 'white').save(self.new / 'screen.png')
        self.write_manifest(self.new)
        self.assertFalse(compare(self.old, self.new)['passed'])

    def test_tampering(self):
        (self.new / 'screen.png').write_bytes(b'broken')
        with self.assertRaisesRegex(ValueError, 'changed after capture'):
            compare(self.old, self.new)

    def test_missing_screen(self):
        self.write_manifest(self.new, id='different')
        with self.assertRaisesRegex(ValueError, 'Screen sets differ'):
            compare(self.old, self.new)

    def test_incomplete(self):
        path = self.new / 'manifest.json'
        data = json.loads(path.read_text()); data['status'] = 'failed'
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, 'Incomplete capture'):
            compare(self.old, self.new)

    def test_fixture_determinism(self):
        a, b = self.root / 'a.epub', self.root / 'b.epub'
        epub(a); epub(b)
        self.assertEqual(a.read_bytes(), b.read_bytes())

    def test_every_sleep_capture_waits_for_completion_then_panel_quiet(self):
        suite = json.loads((HERE / 'screens.json').read_text())
        for group in suite['groups']:
            for machine in group.get('machines', ['x3', 'x4']):
                steps = steps_for(group, machine)
                for screen in group['screens']:
                    if machine not in screen.get('machines', ['x3', 'x4']):
                        continue
                    start = steps.index('mark:before-' + screen['id'])
                    end = steps.index('shot:' + screen['id'])
                    interval = steps[start:end]
                    with self.subTest(group=group['id'], machine=machine, screen=screen['id']):
                        if screen['activity'] == 'Sleep':
                            self.assertEqual(interval[-2:], ['wait-log:Entering deep sleep', 'wait-quiet:1'])
                        else:
                            self.assertNotIn('wait-log:Entering deep sleep', interval)

    def test_sleep_completion_must_precede_capture_in_its_own_interval(self):
        run = self.root / 'case/run'
        run.mkdir(parents=True)
        Image.new('RGB', (3, 2), 'white').save(run / 'sleep.png')
        rows = [
            {'step': 'mark', 'name': 'before-sleep', 't': 4, 'guest_ms': 4000,
             'panel_refreshes': 0, 'panel_refresh_ms': 0},
            {'step': 'shot', 'name': 'sleep', 't': 6, 'file': 'sleep.png',
             'pixels_sha256': 'pixels', 'size': [3, 2]},
            {'step': 'mark', 'name': 'after-sleep', 't': 7, 'guest_ms': 7000,
             'panel_refreshes': 0, 'panel_refresh_ms': 0},
        ]
        (run / 'run.jsonl').write_text('\n'.join(map(json.dumps, rows)))
        group = {'screens': [{'id': 'sleep', 'activity': 'Sleep'}]}
        for timestamp in [None, 3, 6.5, 5]:
            logs = [{'t': 4, 'line': 'Entering activity: Sleep'}]
            if timestamp is not None:
                logs.append({'t': timestamp, 'line': '[MAIN] Entering deep sleep'})
            logs.sort(key=lambda row: row['t'])
            (run / 'console.jsonl').write_text('\n'.join(map(json.dumps, logs)))
            with self.subTest(completion_time=timestamp):
                if timestamp == 5:
                    self.assertEqual(summarize(run, group, 'x3')[0]['activity_observed'], 'Sleep')
                else:
                    with self.assertRaisesRegex(RuntimeError, 'sleep completion missing'):
                        summarize(run, group, 'x3')

    def test_startup_home_before_ready_is_extracted(self):
        run = self.root / 'case/run'
        run.mkdir(parents=True)
        Image.new('RGB', (3, 2), 'white').save(run / 'home.png')
        rows = [
            {'step': 'mark', 'name': 'before-home', 't': 4, 'guest_ms': 4000,
             'panel_refreshes': 2, 'panel_refresh_ms': 600},
            {'step': 'shot', 'name': 'home', 't': 5, 'file': 'home.png',
             'pixels_sha256': 'pixels', 'size': [3, 2]},
            {'step': 'mark', 'name': 'after-home', 't': 6, 'guest_ms': 6000,
             'panel_refreshes': 2, 'panel_refresh_ms': 600},
        ]
        logs = [{'t': 1, 'line': 'Entering activity: Home'},
                {'t': 2, 'line': '[EPD] displayBuffer mode=2 off=0'},
                {'t': 3, 'line': '[EPD] displayBuffer mode=1 off=0'}]
        (run / 'run.jsonl').write_text('\n'.join(map(json.dumps, rows)))
        (run / 'console.jsonl').write_text('\n'.join(map(json.dumps, logs)))
        (run.parent / 'panel.trace').write_text('uc8279_refresh first\nuc8279_refresh second\n')
        result = summarize(run, {'screens': [{'id': 'home', 'activity': 'Home'}]}, 'x3')[0]
        self.assertEqual(result['refresh_requests'], [])
        self.assertEqual(result['panel_refreshes'], 0)
        self.assertEqual(result['startup'], {'refresh_requests': ['fast', 'half'],
                         'controller_refreshes': ['uc8279_refresh first', 'uc8279_refresh second'],
                         'panel_refreshes': 2})

    def test_boot_completion_log_after_marker(self):
        run = self.root / 'case/run'
        run.mkdir(parents=True)
        Image.new('RGB', (3, 2), 'white').save(run / 'boot.png')
        records = [
            {'step': 'mark', 'name': 'before-boot', 't': 0, 'guest_ms': 0,
             'panel_refreshes': 0, 'panel_refresh_ms': 0},
            {'step': 'shot', 'name': 'boot', 't': 1, 'file': 'boot.png',
             'pixels_sha256': 'pixels', 'size': [3, 2]},
            {'step': 'mark', 'name': 'after-boot', 't': 1.001, 'guest_ms': 300,
             'panel_refreshes': 1, 'panel_refresh_ms': 300},
        ]
        logs = [{'t': .1, 'line': 'Entering activity: Boot'},
                {'t': .2, 'line': '[EPD] displayBuffer mode=2 off=0'},
                {'t': 1.002, 'line': '[SSD1677] HALF refresh 300ms (ctrl2=0xd7, seq)'}]
        (run / 'run.jsonl').write_text('\n'.join(map(json.dumps, records)))
        (run / 'console.jsonl').write_text('\n'.join(map(json.dumps, logs)))
        group = {'screens': [{'id': 'boot', 'activity': 'Boot'}]}
        result = summarize(run, group, 'x4')[0]
        self.assertEqual(result['controller_refreshes'], ['half (ctrl2=0xd7, seq)'])
        self.assertEqual(result['refresh_requests'], ['fast'])
        logs.append({'t': 1.1, 'line': '[SSD1677] FAST refresh 300ms (ctrl2=0xfc, seq)'})
        (run / 'console.jsonl').write_text('\n'.join(map(json.dumps, logs)))
        with self.assertRaisesRegex(RuntimeError, 'boot controller log/count mismatch'):
            summarize(run, group, 'x4')


if __name__ == '__main__':
    unittest.main()
