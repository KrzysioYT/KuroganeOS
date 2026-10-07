#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
create = (root / "scripts" / "create-virtualbox-vm.ps1").read_text(encoding="utf-8")
repair = (root / "scripts" / "repair-virtualbox-boot.ps1").read_text(encoding="utf-8")
smoke = (root / "scripts" / "smoke-virtualbox-iso.ps1").read_text(encoding="utf-8")
docs = (root / "docs" / "VIRTUALBOX.md").read_text(encoding="utf-8")

for name, text in {
    "create helper": create,
    "repair helper": repair,
    "smoke helper": smoke,
}.items():
    assert "'--keyboard', 'ps2'" in text, f"{name} must force a PS/2 keyboard"
    assert "'--mouse', 'ps2'" in text, f"{name} must force a PS/2 mouse"

assert "keyboard=\"[^\"]*PS2[^\"]*\"" in create
assert "mouse=\"[^\"]*PS2[^\"]*\"" in create
assert "PS/2 Mouse" in docs
assert "USB Tablet" in docs

print("VirtualBox PS/2 input profile: PASS")
