# Windows runtime inventory repair for Flux 8

> Zapis historyczny / dowód dla wskazanej rewizji. Nie jest instrukcją
> bieżącego wydania. Aktualny stan: [DEVELOPMENT_STATE.md](../../DEVELOPMENT_STATE.md).

The native Windows builder listed only 11 userspace executables; Linux listed
33. Windows deleted its staging root before building, so accountd, sessiond,
notificationd and other PID 1 services were absent from the generated overlay.
This is a concrete build defect and a possible explanation for the reported
VirtualBox installed-session failures; fresh VirtualBox runtime evidence is
still required to establish the complete cause of those failures.

The Windows build now includes all 33 Linux inventory entries with matching
sources, destinations and assembly/C mode. It uses scalar code generation and
rejects SIMD registers in disassembly, matching the Linux user runtime policy.

Regression coverage checks source existence, full inventory equality, all PID 1
service paths, and rejection of the former 11-entry list, an incorrect sessiond
destination, and accountd removed from both builds. The normal host suite and
8.0 qualification workflow run this check. CI also parses the PowerShell script.

Local validation: five parity regression tests, existing Flux application smoke
contract, diff whitespace check, and full Linux release build with --no-image
passed. Native Windows execution was unavailable locally (no PowerShell or
Windows cross-toolchain). This does not qualify a Windows/VirtualBox release.

Separately, commit 7f06237 passed the real authenticated Files/Terminal QEMU
USB-only job and Flux sanitizer job in Actions run 40163835705 after repairing
the Dock coordinate following task-count recentering.

Rebuild media from the repaired source before testing Windows. Existing installed
roots need their system files updated through the installation/repair process;
attaching a new ISO alone does not replace an installed root. Preserve user data.
The 8.0 completion checklist remains open; do not label the candidate stable.

## macOS parity correction, 2026-10-25

The 2026-10-25 macOS build reported by a developer produced valid bootable
IMG/ISO but skipped the same 22 runtime programs previously omitted on
Windows. The macOS `build-macos.sh` inventory still included only 11 entries,
whereas the Linux and repaired Windows inventories contain 33. The build now
includes the full inventory, uses the same no-SIMD userspace flags, inspects
linked assembly with cross-objdump, and performs a mandatory overlay check.
The installer also checks reused staging under `--no-build`, rejecting an
incomplete image instead of packaging missing services. Runtime interaction
must still be verified on real Apple Silicon/Intel macOS with QEMU.
