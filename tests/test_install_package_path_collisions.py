"""Installer packaging must reject casefold and file/directory path conflicts."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "install_pkg", ROOT / "scripts/build-install-package.py")
pkg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pkg)


class CollisionTests(unittest.TestCase):
    def test_casefold_collision(self):
        rows = [(pkg.DEST_ROOT, "/APPS/READ.ME", b"a"),
                (pkg.DEST_ROOT, "/apps/read.me", b"b")]
        with self.assertRaisesRegex(ValueError, "colliding package path"):
            pkg.serialize_package(rows)

    def test_file_directory_ancestor_collision_both_orders(self):
        for rows in (
            [(pkg.DEST_ROOT, "/APPS", b"a"),
             (pkg.DEST_ROOT, "/APPS/ONE.ELF", b"b")],
            [(pkg.DEST_ROOT, "/APPS/TOOLS/ONE", b"a"),
             (pkg.DEST_ROOT, "/APPS/TOOLS", b"b")],
        ):
            with self.subTest(rows=rows):
                with self.assertRaisesRegex(ValueError, "colliding package path"):
                    pkg.serialize_package(rows)

    def test_nearby_names_remain_distinct(self):
        package, count = pkg.serialize_package([
            (pkg.DEST_ROOT, "/APPS/ONE.ELF", b"one"),
            (pkg.DEST_ROOT, "/APPS2/ONE.ELF", b"two"),
        ])
        self.assertEqual(count, 2)
        self.assertGreater(len(package), 64)

    def test_separate_volumes_remain_independent(self):
        payload, count = pkg.serialize_package([
            (pkg.DEST_ROOT, "/EFI/BOOT/BOOT.ELF", b"root"),
            (pkg.DEST_ESP, "/EFI/BOOT/BOOT.ELF", b"esp"),
        ])
        self.assertEqual(count, 2)
        self.assertGreater(len(payload), 64)


if __name__ == "__main__":
    unittest.main()
