# Steel: bounded MSI-X route groups

`kernel/drivers/pci_msix_group.hpp` extends the existing single-vector transport
with one function owner and one to eight independently leased vectors. The
public driver API targets the current Local APIC. It does not implement CPU
affinity changes, SMP interrupt synchronization or multiple VirtIO queues.

## Ownership and ordering

Initialize a `pci::msix::RouteGroup` to zero and pass distinct table indices and
non-null handlers to `enable_group()`, together with complete, bounded,
driver-owned table/PBA BAR mappings. The existing region validator checks both
spans, BAR identities and table/PBA overlap. Every handler/vector is allocated
before the function can generate MSI-X messages. Keep the endpoint's interrupt
sources stopped until `enable_group()` has returned the installed group.

The transaction rejects duplicate indices/vectors, reserved vectors, invalid
xAPIC destinations, malformed layouts and enabled functions. Unselected table
entries must already be masked; otherwise it returns `UnmaskedEntry` without
writing the device. A reset endpoint normally satisfies this condition. A live
output object is rejected without overwriting its ownership records.

Programming masks the function and every selected entry, writes all address/data
pairs, drains posted table writes with a device read, suppresses INTx, enables
MSI-X under the function mask, unmasks the selected entries, drains again, then
unmasks the function. Original entry words and PCI Command/control are saved.

The driver must serialize the lifecycle, stop device interrupt sources and
drain in-flight handlers before calling `disable_group()`. Teardown validates
**all** vector generations before touching registers, then masks/disables the
function, restores entries and control, releases handlers/vectors and finally
restores PCI Command. This API alone is not a hot-unplug synchronization barrier.

If allocation or validation fails, every allocated vector is rolled back. If a
release fails, the output retains the remaining leases. Likewise, a partial
teardown retains unreleased vectors and keeps INTx suppressed until a retry
completes. Callers must inspect `group.count` even after a failed enable and
retain the associated resources until `disable_group()` succeeds. They must
never unmap BARs while the group still owns device state.

## Evidence and scope

`tests/test_pci_msix_group.cpp` exercises the production programming and route
APIs with host PCI/MMIO adapters and the real generation-safe vector allocator.
It covers two distinct handlers, one/eight-vector bounds, partial exhaustion,
programming rollback, failed-release retry, stale/reused/duplicate leases,
layout validation, unowned entries and complete register restoration.

The Steel MSI-X workflow now has separate `single` and `group` jobs. Group mode
uses `inject-steel-msix-runtime.py --group` to invoke the release-linked Intel
82574L qualifier. Two table entries remain installed while the real endpoint's
OTHER-source selector alternates between them. Each of two rounds requires a
new interrupt on the selected IDT vector and no increment on the other vector.
Teardown must release both generations, restore the vector count and reject a
stale group. Serial evidence includes four `[MSIX-GROUP] entry=` records and a
teardown result. The workflow checks failure markers before success and retains
raw serial/QEMU logs for each mode.

The qualifier masks and acknowledges the source, then waits for the maximum
EITR interval using the real PIT clock before changing the OTHER selector or
releasing a route. A finite poll limit rejects a stalled clock. This also
applies to single-vector teardown. The initial local group run delivered to
both vectors but failed on a repeated event: delayed notifications could
overlap the next source assignment. The corrected gate drains those messages
while the original handlers and mappings remain owned, and retains ownership
if draining fails. QEMU's behavior is visible in its
[EITR notification implementation](https://github.com/qemu/qemu/blob/master/hw/net/e1000e_core.c).

This qualifies PCI transport only when the runtime gate passes. It does not
prove independent RX/TX interrupt sources, production-driver group adoption,
SMP, or completion of Steel. The existing production VirtIO-net shared-vector
path remains as documented in [VIRTIO_NET_LIFECYCLE.md](VIRTIO_NET_LIFECYCLE.md).

Local validation and exact-source Actions qualification are recorded separately
below; historical single-vector results do not qualify this new group API.

## Local validation, 2026-09-14

Tested worktree based on `cb352b7aa78105a3b446b354cd0376b96010ac6d`:

- Native Windows host regressions passed: hardware-vector concurrency,
  existing single MSI/MSI-X transactions, new group transactions/lifecycle,
  Device/Driver core and SDK ABI. MSI/MSI-X tests used LLVM-MinGW with warnings
  as errors; the original single-vector test mains were linked with the new
  group's host hardware adapters. No privileged host PCI access was executed.
- Release kernel built with GCC 15.2.0 via Windows PowerShell 5.1. The final
  single/group/uninjected variants were relinked from that build after compiling
  the changed translation units with the same recorded commands. ELF checks
  rejected undefined symbols, unsupported relocations and writable executable
  segments. Native FAT32 boot-image verification passed for all three variants.
- QEMU 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`), q35/TCG, one CPU, 1 GiB RAM,
  EDK2 firmware, a normal E1000 NIC and a dedicated e1000e endpoint: three
  independent group boots **PASS** without tracing after the drain fix. Each
  boot delivered twice on vector 64 and twice on vector 65, released both
  leases and rejected stale group cleanup. Single-vector regression **PASS**.
- Release-version parsing, network-smoke parser cases, both injector modes
  with duplicate-injection rejection, workflow YAML and every matrix shell
  step's syntax passed.

Logs are retained locally under `build/qualification/local-msix-group-final-1/`
through `local-msix-group-final-3/` and `local-msix-single/`. The initial failed
group attempt remains in `local-msix-group-initial-failure/`. Build commands and
checks are in `build/logs/steel-group-kernel.log`, `steel-group-native.log`,
`steel-single-native.log` and `steel-production-native.log`.

| Final kernel variant | SHA-256 |
| --- | --- |
| Two-vector qualifier | `3fafcfee6f827e705df351c5db7f90fe76f7ba90c0ed2118a1e8aa7dcb8a9608` |
| Single-vector qualifier | `af7935b42e44b752f4e67c4bd299a8d7a1b9de24c919a0bdcf822d7f7d0288a8` |
| Uninjected release kernel | `e0797375f6b57872e32c7a15d92fe518e97c03b7616c4fb8e4bf4ad7b54f798e` |

**Full release qualification is still pending.** WSL2 could not start because
host virtualization was unavailable. The local transport images contain an
EFI/FAT32 boot volume, not the canonical GPT persistent root or installer
payload. On the uninjected image, APIC, kernel preemption, E1000 DHCP and gateway
ICMP passed, then PID 1 startup failed because `/system/init` was absent. This
is retained in `local-msix-production/` as an incomplete full-OS fixture, not a
successful production closeout. The full Linux host/media suite, rootfs boot
and exact-source Actions matrix have not been run for this candidate. The
unchanged VirtIO host fixture also cannot compile against native UCRT's missing
`std::aligned_alloc`; no pass is assigned to that attempted native test.
