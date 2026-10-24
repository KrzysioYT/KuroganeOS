# Windows VirtualBox current Flux-8 verification

From D:\KuroganeOS run:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-flux8-virtualbox.ps1

Default: fast-forward codex/road-to-15-stability without force/reset,
rebuild the latest VirtualBox ISO, print SHA256 and start/reuse the
powered-off VM. Unregistered settings files are recovered; orphaned VDI
directories are never deleted or overwritten.

Use -NoBuild only when intentionally testing a possibly stale ISO.
Use -SkipGitUpdate for developers with local working-tree changes.

After login and Files click inspect serial output:

    .\scripts\check-flux8-ui.ps1 -Name KuroganeOS-Flux8-Modern

Do not call version 8.0.0 stable until exact-SHA QEMU tests and real
VirtualBox user interaction complete. Current version remains 7.0.0-dev.

The native VM runner restores ErrorActionPreference after probing an absent
VM; Windows PowerShell 5.1 can otherwise throw NativeCommandError on the
expected VBoxManage not-found stderr, preventing recovery/new VM creation.
