"""Activity lifecycle regression cases from current and historical recordings."""
import json
import unittest
from pathlib import Path
from capture import activity_at


class ActivityTraceTests(unittest.TestCase):
    def trace(self, *lines):
        return [{'t': i, 'line': line} for i, line in enumerate(lines)]

    def test_retained_clock_return_trace(self):
        data = json.loads((Path(__file__).with_name('activity_return_trace.json')).read_text())
        for shot in data['shots']:
            with self.subTest(screen=shot['name']):
                self.assertEqual(activity_at(data['console'], shot['t']), shot['activity'])

    def test_legacy_stack_messages_do_not_pop_twice(self):
        rows = self.trace('Entering activity: Home', 'Pushed to activity stack, new size = 1',
                          'Entering activity: Settings', 'Exiting activity: Settings',
                          'Popped from activity stack, new size = 0')
        self.assertEqual(activity_at(rows, 2), 'Settings')
        self.assertEqual(activity_at(rows, 4), 'Home')

    def test_same_named_nested_instances_return_one_at_a_time(self):
        rows = self.trace('Entering activity: CrossPointWebServer',
                          'Entering activity: NetworkModeSelection',
                          'Exiting activity: NetworkModeSelection',
                          'Entering activity: CrossPointWebServer',
                          'Entering activity: WifiSelection', 'Exiting activity: WifiSelection',
                          'Exiting activity: CrossPointWebServer', 'Exiting activity: CrossPointWebServer')
        self.assertEqual(activity_at(rows, 5), 'CrossPointWebServer')
        self.assertEqual(activity_at(rows, 6), 'CrossPointWebServer')
        self.assertIsNone(activity_at(rows, 7))

    def test_suspended_parent_cleanup_keeps_current_child(self):
        rows = self.trace('Entering activity: Reader', 'Entering activity: Menu',
                          'Exiting activity: Reader', 'Exiting activity: Menu', 'Entering activity: Home')
        self.assertEqual(activity_at(rows, 2), 'Menu')
        self.assertEqual(activity_at(rows, 4), 'Home')

    def test_reset_does_not_resume_pre_reset_activity(self):
        rows = self.trace('Entering activity: Reader', 'ESP-ROM:esp32c3-api1-20210207',
                          'Entering activity: Boot', 'Exiting activity: Boot', 'Entering activity: Crash')
        self.assertIsNone(activity_at(rows, 3))
        self.assertEqual(activity_at(rows, 4), 'Crash')

    def test_incomplete_lifecycle_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'exit without enter'):
            activity_at(self.trace('Exiting activity: Missing'), 1)

    def test_events_after_shot_do_not_change_activity(self):
        rows = self.trace('Entering activity: Reader', 'Entering activity: Menu', 'Exiting activity: Menu')
        self.assertEqual(activity_at(rows, 1), 'Menu')
