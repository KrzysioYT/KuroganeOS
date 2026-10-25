"""Regression tests: missing runtime programs must block macOS ISO publication."""
import importlib.util
import pathlib
import struct
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "userspace_overlay", ROOT / "scripts/verify-userspace-overlay.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def elf(machine=62, kind=2):
    blob = bytearray(64)
    blob[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<HH", blob, 16, kind, machine)
    return bytes(blob)


class OverlayPreflight(unittest.TestCase):
    def test_inventory_is_full(self):
        paths = mod.inventory_paths()
        self.assertEqual(len(paths), 33)
        for required in ("system/accountd", "system/sessiond", "system/notifd",
                         "system/clipd", "system/init"):
            self.assertIn(required, paths)

    def test_full_and_missing_overlay(self):
        expected = ["system/accountd", "system/sessiond", "gui/launcher"]
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            for p in expected:
                dest = root / p
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(elf())
            errors, count = mod.validate(root, expected)
            self.assertEqual((errors, count), ([], 3))
            (root / "system/sessiond").unlink()
            errors, _ = mod.validate(root, expected)
            self.assertEqual(errors, ["system/sessiond: MISSING"])

    def test_invalid_elf_cannot_pass(self):
        with tempfile.TemporaryDirectory() as td:
            p = pathlib.Path(td) / "system/accountd"
            p.parent.mkdir(parents=True)
            for raw in (b"", b"\x7fELF", elf(machine=183), elf(kind=3)):
                p.write_bytes(raw)
                errors, _ = mod.validate(pathlib.Path(td), ["system/accountd"])
                self.assertTrue(errors)


if __name__ == "__main__":
    unittest.main()
