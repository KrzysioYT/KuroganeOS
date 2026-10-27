#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
create = (root / "scripts" / "create-virtualbox-vm.ps1").read_text(encoding="utf-8")
repair = (root / "scripts" / "repair-virtualbox-boot.ps1").read_text(encoding="utf-8")
smoke = (root / "scripts" / "smoke-virtualbox-iso.ps1").read_text(encoding="utf-8")
contract = (root / "scripts" / "lib" / "virtualbox-vm-contract.ps1").read_text(encoding="utf-8")
regression = (root / "tests" / "test_create_virtualbox_vm_contract.ps1").read_text(encoding="utf-8")
docs = (root / "docs" / "VIRTUALBOX.md").read_text(encoding="utf-8")

for name, text in {
    "create helper": create,
    "repair helper": repair,
    "smoke helper": smoke,
}.items():
    assert "'--keyboard', 'ps2'" in text, f"{name} must force PS/2 keyboard"
    assert "'--mouse', 'ps2'" in text, f"{name} must force PS/2 mouse"

# Modern VBoxManage versions omit input fields in showvminfo --machinereadable.
# Static policy must check the actual verifier, not retired inline regex.
assert "Test-KuroganeVBoxVmContract -MachineInfo $machine" in create
assert "if ($fields.ContainsKey('keyboard'))" in contract
assert "if ($fields.ContainsKey('mouse'))" in contract
assert "ps2keyboard" in contract and "ps2mouse" in contract
assert "PS/2 keyboard" in contract and "PS/2 mouse" in contract
assert "Assert-Rejected" in regression
assert "USBTablet" in regression
assert "Assert-Valid" in regression
# Document the two independent input profiles without coupling the test to
# historical capitalization/typography; driver and VM contract remain strict.
assert "PS/2" in docs, "VirtualBox PS/2 fallback must be documented"
assert "USB Tablet" in docs, "USB-only QEMU acceptance must be documented"

print("VirtualBox PS/2 input profile: PASS")
