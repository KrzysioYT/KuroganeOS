#!/usr/bin/env python3
"""Reject truncated or stale guest evidence in the lifecycle acceptance runner."""
import importlib.util
from pathlib import Path
import time
import unittest

spec = importlib.util.spec_from_file_location('lifecycle', Path(__file__).resolve().parents[1] /
    'scripts/qualification/flux-lifecycle-qmp.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
LINE = ('[WM] action=restore id=259 pid=261 app=1 state=0 x=235 y=155 w=760 h=430 '
        'focus=259 windows=3 surfaces=3 bytes=512 sw=1280 sh=800\r\n')


class EvidenceTests(unittest.TestCase):
    def test_serial_noise_and_crlf(self):
        result = module.records('boot message\r\n' + LINE)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]['id'], 259)
        self.assertEqual(result[0]['action'], 'restore')

    def test_partial_tail_not_counted(self):
        self.assertEqual(module.records(LINE.rstrip()), [])
        self.assertEqual(len(module.records(LINE + LINE[:40])), 1)

    def test_missing_field_is_failure(self):
        with self.assertRaises(RuntimeError):
            module.records(LINE.replace('pid=261 ', ''))

    def test_invalid_numeric_state_is_failure(self):
        with self.assertRaises(ValueError):
            module.records(LINE.replace('state=0', 'state=invalid'))

    def harness(self, lines):
        h = module.Harness.__new__(module.Harness)
        h.results = []
        h.read = lambda: module.records(lines)
        h.deadline = time.monotonic() + .001
        return h

    def test_stale_transition_cannot_satisfy_next_action(self):
        h = self.harness(LINE)
        with self.assertRaises(TimeoutError):
            h.wait(1, 'restore', window=259)

    def test_wrong_generation_or_app_cannot_satisfy_action(self):
        for window, app in ((515, 1), (259, 2)):
            with self.assertRaises(TimeoutError):
                self.harness(LINE).wait(0, 'restore', app=app, window=window)

    def test_fresh_transition_matches_exact_identity(self):
        h = self.harness(LINE)
        h.deadline = time.monotonic() + 1
        self.assertEqual(h.wait(0, 'restore', window=259)['pid'], 261)


if __name__ == '__main__':
    unittest.main()
