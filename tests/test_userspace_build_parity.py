"""Reject incomplete Windows/Linux runtime images before media qualification."""
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


def validate(linux, windows, init):
    assert linux and windows, 'Empty userspace build inventory'
    assert len({row[0] for row in linux}) == len(linux), 'Duplicate build names'
    assert len({row[2] for row in linux}) == len(linux), 'Duplicate output paths'
    assert sorted(linux) == sorted(windows), 'Windows/Linux userspace inventory differs'
    required = set(re.findall(r'^#define \w+_PATH "/(system/[^"\n]+)"', init, re.M))
    assert required, 'PID 1 service inventory not found'
    assert required <= {row[2] for row in windows}, 'Missing PID 1 runtime service'


class UserspaceBuildParity(unittest.TestCase):
    def setUp(self):
        self.ps = (ROOT / 'scripts/build.ps1').read_text()
        self.sh = (ROOT / 'scripts/build-linux.sh').read_text()
        self.linux = linux_inventory(self.sh)
        self.windows = windows_inventory(self.ps)
        self.init = (ROOT / 'userspace/system/init/main.c').read_text()

    def test_inventory_and_sources(self):
        validate(self.linux, self.windows, self.init)
        for _, source, _, _ in self.windows:
            self.assertTrue((ROOT / source).is_file(), source)

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
