"""Reject incomplete macOS/Windows/Linux runtimes before media qualification."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def linux_inventory(text):
    block = text.split('applications=(', 1)[1].split('\n)', 1)[0]
    return [tuple(row.split('|')) for row in re.findall(r'"([^"\n]+)"', block)]


def windows_inventory(text):
    block = text.split('$applications = @(', 1)[1].split('\n    )', 1)[0]
    rows = re.findall(
        r"Name = '([^']+)'; Source = '([^']+)'; Output = '([^']+)'; Assembly = \$(true|false)",
        block,
    )
    return [(name, 'userspace/' + source.replace('\\', '/'), output,
             'asm' if assembly == 'true' else 'c')
            for name, source, output, assembly in rows]


def validate(linux, windows, init, macos=None):
    assert linux and windows, 'Empty userspace build inventory'
    assert len({row[0] for row in linux}) == len(linux), 'Duplicate build names'
    assert len({row[2] for row in linux}) == len(linux), 'Duplicate output paths'
    assert sorted(linux) == sorted(windows), 'Windows/Linux userspace inventory differs'
    if macos is not None:
        assert sorted(linux) == sorted(macos), 'macOS/Linux userspace inventory differs'
        assert len({row[0] for row in macos}) == len(macos), 'Duplicate macOS build names'
        assert len({row[2] for row in macos}) == len(macos), 'Duplicate macOS output paths'
    required = set(re.findall(r'^#define \w+_PATH "/(system/[^"\n]+)"', init, re.M))
    assert required, 'PID 1 service inventory not found'
    assert required <= {row[2] for row in windows}, 'Missing PID 1 runtime service'


class UserspaceBuildParity(unittest.TestCase):
    def setUp(self):
        self.ps = (ROOT / 'scripts/build.ps1').read_text()
        self.mac_script = (ROOT / 'scripts/build-macos.sh').read_text()
        self.mac = linux_inventory(self.mac_script)
        self.sh = (ROOT / 'scripts/build-linux.sh').read_text()
        self.linux = linux_inventory(self.sh)
        self.windows = windows_inventory(self.ps)
        self.init = (ROOT / 'userspace/system/init/main.c').read_text()

    def test_inventory_and_sources(self):
        validate(self.linux, self.windows, self.init, self.mac)
        for _, source, _, _ in self.windows:
            self.assertTrue((ROOT / source).is_file(), source)

    def test_old_incomplete_macos_inventory_is_rejected(self):
        with self.assertRaisesRegex(AssertionError, 'macOS/Linux userspace inventory differs'):
            validate(self.linux, self.windows, self.init, self.mac[:11])

    def test_macos_missing_account_or_session_is_rejected(self):
        for missing in ('system/accountd', 'system/sessiond', 'system/notifd'):
            broken = [row for row in self.mac if row[2] != missing]
            with self.assertRaisesRegex(AssertionError, 'macOS/Linux userspace inventory differs'):
                validate(self.linux, self.windows, self.init, broken)

    def test_macos_runtime_compiler_and_package_guard(self):
        for flag in ('-mno-mmx', '-mno-sse', '-msoft-float'):
            self.assertIn(flag, self.mac_script)
        self.assertIn('"$objdump" -d "$target"', self.mac_script)
        self.assertIn('verify-userspace-overlay.py', self.mac_script)
        installer = (ROOT / 'scripts/build-installer-macos.sh').read_text()
        self.assertIn('verify-userspace-overlay.py', installer)

    def test_foundation_image_checks_the_complete_runtime_payload(self):
        source = (ROOT / 'tests/test_root_volume_image.cpp').read_text()
        block = source.split('kRequiredRuntimePaths[] = {', 1)[1].split('};', 1)[0]
        actual = re.findall(r'"(/[^"]+)"', block)
        expected = ['/' + row[2] for row in self.linux]
        expected.extend('/gui/' + name for name in (
            'login', 'launcher', 'terminal', 'files', 'sysmon',
            'perf', 'browser', 'about', 'settings', 'notify'))
        expected.append('/apps/external')
        self.assertEqual(sorted(actual), sorted(expected))
        builder = (ROOT / 'scripts/build-foundation-image.sh').read_text()
        self.assertIn('validate-foundation-root-image.sh', builder)
        mac_builder = (ROOT / 'scripts/build-foundation-image-macos.sh').read_text()
        self.assertIn('validate-foundation-root-image.sh', mac_builder)

    def test_live_package_vfs_supports_directory_enumeration(self):
        source = (ROOT / 'kernel/install/package_vfs.cpp').read_text()
        self.assertIn('filesystem->operations.readdir = read_directory', source)
        self.assertNotIn('unsupported_readdir', source)
        self.assertIn('tests/test_package_vfs.cpp', (
            ROOT / 'scripts/run-host-tests.sh').read_text())

    def test_macos_headless_runner_selects_try_not_install(self):
        runner = (ROOT / 'scripts/run-qemu-macos.sh').read_text()
        self.assertIn('installer: select setup mode:', runner)
        self.assertIn('"data": key', runner)
        self.assertIn('snapshot=on', runner)
        self.assertIn('if ! $try_selected', runner)
        self.assertIn('[TEST] red_flux_login_surface: PASS', runner)
        self.assertIn('[TEST] live_package_root: PASS', runner)
        self.assertIn('[TEST] setup_try_mode: PASS', runner)
        self.assertIn('[TEST] red_flux_owned_session: PASS', runner)
        self.assertIn('[SESSION] flux_home_first_frame: PASS', runner)
        self.assertIn('[TEST] red_flux_home_surface: PASS', runner)

    def test_normal_boot_starts_flux_compositor_before_pid1(self):
        main = (ROOT / 'kernel/main.cpp').read_text()
        init_spawn = main.index('process::spawn_init("/system/init", &init_pid)')
        compositor_start = main.index(
            'if (!kurogane_start_desktop_session())', init_spawn - 1500)
        self.assertLess(compositor_start, init_spawn)
        self.assertIn(
            'Flux compositor unavailable; PID 1 will use visible recovery',
            main[compositor_start:init_spawn],
        )

    def test_try_mode_boots_without_an_install_target_disk(self):
        main = (ROOT / 'kernel/main.cpp').read_text()
        installer = (ROOT / 'kernel/install/installer.cpp').read_text()
        self.assertIn(
            '[TEST] installer_generic_storage: SKIP (live Try needs no disk)',
            main,
        )
        self.assertNotIn(
            'no supported block device discovered for installation',
            main,
        )
        self.assertIn(
            'if (try_selected || target_disk_unavailable)', installer)
        self.assertLess(
            installer.index('initialize_live_package(package_bytes, package_size)'),
            installer.index('show_install_unavailable();'),
        )
        self.assertIn('show_install_unavailable();', installer)
        self.assertIn('[TEST] installer_target_disk_unavailable: PASS', installer)
        self.assertIn('[TEST] setup_no_disk_recovery: PASS', installer)

    def test_qemu_smoke_requires_requested_screenshot_frames(self):
        runner = (ROOT / 'scripts/smoke-uefi-iso-qemu.sh').read_text()
        capture = (ROOT / 'scripts/qualification/qmp-screendump.py').read_text()
        self.assertIn('--screendump-after-marker', runner)
        self.assertIn('capture_pending_screenshots', runner)
        self.assertIn('screenshots_complete', runner)
        self.assertIn('"screendump"', capture)
        self.assertIn('refusing to overwrite screenshot', capture)

    def test_legacy_windows_inventory_is_rejected(self):
        with self.assertRaisesRegex(AssertionError, 'inventory differs'):
            validate(self.linux, self.windows[:11], self.init)

    def test_missing_service_in_both_builds_is_rejected(self):
        missing = [row for row in self.linux if row[2] != 'system/accountd']
        with self.assertRaisesRegex(AssertionError, 'Missing PID 1'):
            validate(missing, missing, self.init)

    def test_wrong_service_destination_is_rejected(self):
        wrong = [(*row[:2], 'system/wrong', row[3]) if row[0] == 'sessiond'
                 else row for row in self.windows]
        with self.assertRaisesRegex(AssertionError, 'inventory differs'):
            validate(self.linux, wrong, self.init)

    def test_scalar_codegen_and_inspection(self):
        body = self.ps.split('function Build-UserApplications {', 1)[1].split(
            'function Build-DeveloperSdk', 1)[0]
        for flag in ('-mno-mmx', '-mno-sse', '-msoft-float'):
            self.assertIn(flag, body)
            self.assertIn(flag, self.sh)
        self.assertIn("$tools.OBJDUMP '-d' $outputRelative", body)
        self.assertIn('%(xmm|ymm|zmm|mm)[0-9]+', body)
        self.assertIn("x86_64-elf-objdump.exe", self.ps)


if __name__ == '__main__':
    unittest.main()
