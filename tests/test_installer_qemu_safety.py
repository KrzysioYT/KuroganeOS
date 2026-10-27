#!/usr/bin/env python3
"""Installer automation must refuse existing output directories and devices."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('installer_qemu', Path(__file__).resolve().parents[1] /
    'scripts/qualification/installer-qemu.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SafetyTests(unittest.TestCase):
    def test_existing_directory_and_symlink_are_never_reused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            iso = root / 'test.iso'
            iso.write_bytes(b'fixture')
            target = root / 'existing'
            target.mkdir()
            disk = target / 'installed.img'
            disk.write_bytes(b'preserve my disk')
            alias = root / 'alias'
            alias.symlink_to(target, target_is_directory=True)
            for output in (target, alias):
                with mock.patch('sys.argv', ['installer-qemu', '--iso', str(iso), '--output', str(output)]), \
                     mock.patch.object(module, 'firmware', return_value=('code', 'vars')), \
                     mock.patch.object(module.subprocess, 'Popen') as popen:
                    with self.assertRaises(FileExistsError):
                        module.main()
                    popen.assert_not_called()
                self.assertEqual(disk.read_bytes(), b'preserve my disk')

    def test_device_is_not_an_iso(self):
        with tempfile.TemporaryDirectory() as directory, \
             mock.patch('sys.argv', ['installer-qemu', '--iso', '/dev/null', '--output', directory]), \
             mock.patch.object(module.subprocess, 'Popen') as popen:
            with self.assertRaises(SystemExit) as caught:
                module.main()
            self.assertEqual(caught.exception.code, 2)
            popen.assert_not_called()

    def test_guest_installer_failure_cannot_count_as_success(self):
        with tempfile.TemporaryDirectory() as directory:
            serial = Path(directory) / 'serial.log'
            serial.write_text('[TEST] installer_complete: FAIL\n[TEST] installer_complete: PASS\n')
            with self.assertRaises(RuntimeError):
                module.wait_marker(serial, '[TEST] installer_complete: PASS', mock.Mock(),
                                   module.time.monotonic() + 1)


if __name__ == '__main__':
    unittest.main()
