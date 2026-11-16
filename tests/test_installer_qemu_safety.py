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
    def test_installed_dock_record_rejects_damage_and_old_versions(self):
        # Exactly the binary data written by the actual 13-byte v3 profile.
        record = bytearray((0x07, 0xD8, 1, 3, 2, 4, 5, 6, 7))
        checksum = 2166136261
        for byte in record:
            checksum = ((checksum ^ byte) * 16777619) & 0xFFFFFFFF
        record += checksum.to_bytes(4, 'little')
        module.validate_dock_record(bytes(record))
        bad_records = [
            record[:-1],                         # Truncated checksum.
            bytearray(record[:9]),                # Old pre-checksum format.
            bytearray(record),                    # Valid order, corrupt checksum.
            bytearray(record),                    # Valid digest, wrong app order.
            bytearray(record),                    # HOME pin lost.
        ]
        bad_records[2][9] ^= 1
        bad_records[3][3], bad_records[3][4] = bad_records[3][4], bad_records[3][3]
        # Recalculate the digest to prove the order contract is checked
        # independently from checksum integrity.
        swapped_checksum = 2166136261
        for byte in bad_records[3][:9]:
            swapped_checksum = ((swapped_checksum ^ byte) * 16777619) & 0xFFFFFFFF
        bad_records[3][9:] = swapped_checksum.to_bytes(4, 'little')
        bad_records[4][0] &= ~1
        for bad in bad_records:
            with self.subTest(data=bytes(bad).hex()):
                with self.assertRaises(RuntimeError):
                    module.validate_dock_record(bytes(bad))

    def test_password_evidence_requires_rejection_before_session(self):
        prefix = [module.INSTALLED_PROFILE, module.BAD_PASSWORD, module.GOOD_PASSWORD]
        suffix = [module.SESSION_CREATED, module.HOME_FRAME, module.DESKTOP]
        module.verify_password_login('\r\n'.join(prefix + suffix), True)
        module.verify_password_login('\n'.join([prefix[0], prefix[2], *suffix]), False)
        for wrong in (
            [prefix[0], prefix[2], *suffix],  # First boot did not reject a bad password.
            [*prefix[:2], module.SESSION_CREATED, prefix[2], *suffix[1:]],
            [*prefix, *suffix[:-1]],  # Home rendered but the Login handoff did not finish.
            [*prefix, *suffix, module.SESSION_CREATED],  # Unexpected duplicate session.
        ):
            with self.assertRaises(RuntimeError):
                module.verify_password_login('\n'.join(wrong), True)

    def test_theme_evidence_requires_two_real_nondefault_changes(self):
        self.assertEqual(module.saved_theme('[SETTINGS] theme_saved=5\r\n[SETTINGS] theme_saved=4\r\n'), 4)
        for text in ('', '[SETTINGS] theme_saved=4\n',
                     '[SETTINGS] theme_saved=4\n[SETTINGS] theme_saved=4\n',
                     '[SETTINGS] theme_saved=1\n[SETTINGS] theme_saved=0\n'):
            with self.assertRaises(RuntimeError):
                module.saved_theme(text)

    def test_text_scale_evidence_requires_one_supported_nondefault_value(self):
        self.assertEqual(
            module.saved_text_scale('[SETTINGS] text_scale_saved=125\r\n'), 125)
        for text in ('', '[SETTINGS] text_scale_saved=100\n',
                     '[SETTINGS] text_scale_saved=125\n'
                     '[SETTINGS] text_scale_saved=150\n',
                     '[SETTINGS] text_scale_saved=175\n'):
            with self.assertRaises(RuntimeError):
                module.saved_text_scale(text)

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
