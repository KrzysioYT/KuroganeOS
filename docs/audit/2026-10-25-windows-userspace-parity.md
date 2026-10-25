# Windows runtime inventory repair for Flux 8

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
