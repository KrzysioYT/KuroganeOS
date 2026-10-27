from pathlib import Path
root = Path(__file__).resolve().parents[1]
script = (root / "scripts/run-flux8-virtualbox.ps1").read_text()
creator = (root / "scripts/create-virtualbox-vm.ps1").read_text()
for required in ("git.exe pull --ff-only", "-Rebuild -SkipVirtualBoxSmoke",
                 "Get-FileHash", "create-virtualbox-vm.ps1", "registervm",
                 "VMState=", "storageattach", "startvm", "kurogane-serial.log"):
    # Serial log is configured by the native creator, not rewritten here.
    if required == "kurogane-serial.log":
        assert required in creator, "native creator must keep COM1 logging"
    else:
        assert required in script, "missing safe VM operation: " + required
for forbidden in ("unregistervm", "Remove-Item", "rm -", "git.exe reset --hard"):
    assert forbidden not in script, "destructive VM operation: " + forbidden
assert "$savedErrorActionPreference" in script
assert "old" not in script.lower().split("git.exe pull --ff-only")[0], "unexpected legacy checkout"
print("safe Windows Flux8 VM runner static contract: PASS")
