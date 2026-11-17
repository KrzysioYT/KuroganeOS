# KuroganeOS 7.0.0-dev — Iron Shield closeout gate

**Status: OPEN / NOT QUALIFIED.** This is an engineering qualification checklist,
not a declaration that 7.0.0 is complete. The 7.0 runtime identity stays
`7.0.0-dev` until all agreed milestone requirements pass on the same code.

## Baseline and scope

Iron Shield is the driver/device expansion milestone from
[`MASTER_ROADMAP_15.md`](../roadmap/MASTER_ROADMAP_15.md).
Use the current integrated code on `integration/flux8-reconciliation`, not a
pre-integration PR branch. The 7.0 work must preserve the 5.x hardware
interface contracts and the 6.0 scheduler/IPC regressions.

## Release-safe driver policy

The RTL8111/8168 backend is **unqualified experimental code**. The default
image only recognizes its PCI ID and reports that the driver is not operational.
It does **not** reset the device or activate Realtek DMA. For isolated
hardware-development builds, a developer can explicitly compile the network
selector with `-DKUROGANE_EXPERIMENTAL_RTL81XX=1`; this is **not** a release
feature or a hardware-acceptance claim. The host suite runs both the
default/quarantined and experimental selector paths. Neither test replaces
production RTL8168 RX/TX, reset and DMA failure-injection evidence.

Do not remove this guard until the specific supported RTL revisions, real-device
execution and safe resource recovery have been independently qualified.

Existing supported-subset qualification:
[`qualify-7.0-supported-devices.yml`](../../.github/workflows/qualify-7.0-supported-devices.yml)
builds a clean release image and requires real guest interactions for
E1000e + VirtIO block + USB HID + SMP, VirtIO-net + VirtIO block + USB HID + SMP,
and a no-NIC USB-only fallback. The resulting logs must be tied to the exact
candidate source SHA. **These three passing QEMU profiles alone do not qualify
the entire 7.0 milestone.**

## Remaining mandatory engineering work

| Requirement | Evidence for closeout | State |
| --- | --- | --- |
| Intel E1000 and E1000e | Same-SHA DHCP, gateway and recovery on both models | Requalify on candidate |
| VirtIO-net and VirtIO-block | Guest DMA read/write/flush, rollback/failure and coexistence | Requalify on candidate |
| Legacy PCnet | Guest DHCP/gateway and explicit fallback | Requalify on candidate |
| Realtek RTL8111/8168 class | Production RTL81xx driver path, PCI model validation, DMA RX/TX, reset/timeout, link state, negative tests and supported physical execution | **DRAFT CODE / NOT QUALIFIED** — MMIO and DMA path added to PR #60, lacks real RTL8168 hardware runtime proof and power/reset stress |
| Intel HDA + AC'97 | Hardware family selection, production PCM playback and failure fallback | The QEMU gate requires HDA codec and PCM DMA, then AC'97 selection and PCM DMA in a separate boot with a codec-less HDA controller. AC'97 readiness requires completion of a silent PCM descriptor and an acknowledged stop/reset. Physical codec and playback quality remain unverified. |
| USB hubs, multi-device hotplug, wider HID | Enumeration through a hub, detach/re-attach, device churn, simultaneous keyboard/mouse and negative report validation | **DOWNSTREAM BOOT BLOCK I/O / NOT QUALIFIED** — xHCI addresses a full-speed storage child and reads its first sector. Only the tagged QEMU scratch fixture is eligible for a write, flush, readback and restore within the boot probe. CI compares the complete scratch image after QEMU exits and verifies direct HID and direct USB storage. Persistent binding, runtime downstream block I/O, high-speed hub TT scheduling and hub hotplug remain open. Physical hub qualification is blocked without suitable hardware. |
| Device power management | PCI D-state lifecycle, PMCSR/PME and device-specific safe recovery; system-wide suspend/resume only if part of the accepted 7.0 contract | **PARTIAL** — the PR gate boots a separately instrumented image from the exact candidate SHA and requires four pre-bind D0 → D3hot → D0 cycles followed by E1000e DHCP and ICMP. PMCSR transactions check PCI identity and preserve unrelated live controls. Active-driver recovery and physical power transitions remain unverified. |
| Error isolation and hardware inventory | Unsupported hardware explained without claiming functional driver; optional-device failure does not stop boot or damage storage | Needs end-to-end evidence |
| Closeout regression | Full host, UBSan, kernel tests, clean IMG/ISO, real QEMU/OVMF with device-matrix logs, checksums and exact SHA | **OPEN** |

The USB gate also requires three QEMU root-port keyboard detach/reattach cycles
on an instrumented image from the candidate SHA. Each cycle checks release of a
held Shift key, retirement of the old device handle, and F12 press/release
through the new attachment. The host regression checks that Disable Slot
discards deferred transfers for the retired slot while preserving port events
and transfers for the other slot.

A failed downstream slot retirement aborts the hub probe before port disable.
The controller cleanup path must quiesce DMA before releasing its resources.
The host regression verifies that the hub sends no further request after the
child callback reports a retirement failure.

Cold-boot hub probing now powers inactive individually switched ports, waits
for power-good and connection detection, and verifies status before reset.
Overcurrent, active ports and failed control/timer/readback operations stop that
probe. Unpowered ganged hubs need a shared-power policy. CI requires the same
downstream scratch restore and direct HID checks with QEMU port-power enabled.
The separate instrumented image first powers each port off through a USB class
request and verifies readback. The artifact records the source SHA, patch and
image hash.
Physical switched-hub power behavior remains BLOCKED without suitable hardware.

The first cold-boot hub probe visits every eligible descriptor port (at most
15), retiring each child before reusing the companion slot and bulk rings. CI
requires two downstream disks with distinct 8 MiB and 16 MiB capacities, reads
from each, and checks both complete disk hashes after QEMU exits. Only the
existing tagged 8 MiB scratch fixture is eligible for the write/restore probe.
Persistent attachment and downstream hotplug remain open.

RTL81xx cleanup before RX/TX start now preserves ownership when a DMA page
release or MMIO unmap fails. Successfully unmapped suffix pages are removed
from the retained count, and retries release only the remaining resources.
Initialization reports ResourceReleaseFailed and retains the failed device state.
Host/UBSan inject failures for all 18 DMA pages, both MMIO pages and a missing
address space. Active-device teardown and physical RTL reset/DMA qualification
remain BLOCKED without suitable hardware; production quarantine stays enabled.

RTL81xx partial BAR mapping rollback uses the same tracked cleanup. A failed
unmap retains the installed prefix and blocks initialization from overwriting
it. The host/UBSan regression covers second-page mapping failure with failed
rollback, retry, collisions on either page, aligned/unaligned BARs and address
overflow. Colliding mappings belong to their original owner and are retained.

## Rules for closing

1. Do not silently shrink the scope to the already qualified virtual-device
   subset. Unqualified RTL8111/8168 production operation is a feature/qualification
   gap, not merely a documentation issue. The owner may explicitly change the release scope, but no other actor
   may assume that decision.
2. Do not equate PCI enumeration with a working driver. A NIC must exchange
   frames through the production network interface. Hardware tests outside the
   available environment are `BLOCKED` or `NOT TESTED`, never `PASS`.
3. Preserve old regressions and run failures to root cause. No test skipping to
   advance a version number.
4. A final candidate must pass all agreed 7.0 gates at a single SHA, with a
   release-note statement of supported devices and known limitations.
5. Only after a verified 7.0 closeout, proceed with an 8.0 version promotion.
   Do not merge or tag unqualified work simply to remove 7.0 from the roadmap.

See [historical supported-subset audit](../audit/2026-10-24-supported-device-gate.md)
and [consolidation findings](../audit/2026-10-20-consolidation.md)
for earlier limitations, not current test-result substitutes.
