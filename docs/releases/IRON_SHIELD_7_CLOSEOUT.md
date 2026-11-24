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
builds release media and exercises E1000, E1000e, VirtIO-net and PCnet with
VirtIO block, USB HID without PS/2 and SMP. The separate no-NIC job exercises
loopback and USB input, block devices, audio and selected recovery paths. The
workflow records the checked-out source SHA and separate hashes for the
release and instrumented images.

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
| Error isolation and hardware inventory | Unsupported hardware explained without claiming functional driver; optional-device failure does not stop boot or damage storage | **PARTIAL** — the QEMU gate requires an unsupported downstream tablet to be identified and retired on port 1, then a storage read/write/restore on port 2 with direct keyboard/mouse still delivering input. Broader optional-device fault isolation remains open. |
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
probe. Unpowered ganged hubs now use the bounded group policy described below. CI requires the same
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

RTL81xx readiness and production TX/RX now require both DMA engines enabled,
reset deasserted and a readable command register as well as carrier. Host/UBSan
inject stopped, RX-only, TX-only, reset and unreadable command states while
PHY still reports link. Each rejects TX/RX, preserves descriptors and output
buffers, and leaves ring positions/counters unchanged. This guards against
unexpected engine stop; active teardown and physical recovery remain open.

RTL81xx readiness also validates the live PCI vendor/device identity and
requires memory decoding plus bus mastering before reading BAR registers.
The runtime regression supplies an inaccessible BAR with removed/replaced PCI
identity, lost decode/DMA enable or unreadable command state; readiness and
TX/RX reject these states before MMIO, preserving buffers and descriptors.
These are host fault-injection results; physical removal/power recovery remains
BLOCKED and the experimental initialization opt-in remains required.

A downstream configuration with no supported BOT/SCSI interface now
reports an explicit unsupported-interface diagnostic after child retirement.
The separate QEMU negative gate puts a tablet before the supported scratch
disk and requires successful port cleanup, disk access and direct HID input.
This checks boot-time isolation and leaves persistent downstream HID/hotplug
and physical hub qualification open.

Hub reset results now distinguish a verified reset, a failed probe with verified
port disable, and unsafe cleanup. After a reset attempt, an ignored/failed
disable, unreadable final status, a port still enabled/resetting or a failed
child retirement aborts hub enumeration and enters controller DMA cleanup.
The next child cannot reuse a slot/address while the prior port state is
unverified. Host/UBSan cover these failures, re-enable after acknowledgement,
and a reset timeout followed by verified disable. Physical hub fault recovery
remains BLOCKED without suitable hardware.

Unverified control transfers before reset also return the unsafe result,
so an unfinished EP0 operation cannot be followed by another port probe.
Host/UBSan cover failed change acknowledgements and status reads before reset.

Hub port power now distinguishes an unsupported/no-action policy from an
unsafe result after PowerOn. Failed SET_FEATURE, timer or status readback,
unpowered readback, overcurrent and unexpected enable/reset/suspend after power
abort the hub probe and enter controller DMA cleanup instead of continuing to
the next port. An unreadable initial port status also aborts. Host/UBSan inject
these power failures; successful individually powered ports remain a QEMU gate.
The host suite also applies the CI power-off fixture to the current production
probe and rejects missing anchors or duplicate injection; an anchor mismatch
must fail qualification before QEMU. Physical hub power/fault recovery remains
BLOCKED without suitable hardware.

A failed companion Disable Slot now contains the whole xHCI controller, marks
both HID lifecycles and the companion device failed, and stops event polling
before more input can be queued. DMA pages, slot/DCBAA and endpoint rings remain
owned until explicit cleanup/reinitialize. Host/UBSan inject a missing command
completion with both acknowledged halt and halt timeout, checking retained DMA,
unchanged pending events and no subsequent polling work. Physical fault recovery
remains BLOCKED; this does not qualify persistent downstream hotplug.

The no-NIC CI gate also boots a separately instrumented image with real USB
keyboard/mouse and an injected unacknowledged live companion retirement. It
requires actual xHCI halted-status and PCI bus-master-disable readback, retained
slot/DCBAA/DMA ownership and failed HID lifecycles, as well as the login surface
and loopback service. USB is intentionally unavailable after containment; normal
input and hotplug remain mandatory in the uninjected/other qualified profiles.
The artifact records candidate SHA, exact patch and instrumented image hash.
This tests the production containment path against QEMU hardware, not a real
missing command completion or physical hub failure; those remain BLOCKED.

Cold-boot ganged hub power now preflights all (at most 15) ports before any
shared change. Only an entirely unpowered, inactive group can receive PowerOn;
partial power, enabled/reset/suspended ports or overcurrent defer that hub.
The driver requests power for every logical port, waits power-good once and
reads back the entire group before enumeration. An unverified transfer, timer
or post-power state enters controller containment. Already powered groups are
read-only. Host/UBSan cover every transfer failure and each unsafe state.
This is runtime wiring plus host evidence, not ganged-hub hardware qualification:
QEMU's existing individual-port profiles remain required, and a real ganged-hub
profile, physical switching and downstream hotplug remain BLOCKED/unqualified.

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

## 2026-11-19 — RTL81xx descriptor-integrity increment (qualification pending)

The experimental production RX/TX paths now validate each live descriptor's
physical DMA-buffer binding and end-of-ring marker before consuming RX data
or publishing TX work. A mismatch latches a cross-CPU atomic device fault,
returns an interface error, stops new software ring operations and retains
all DMA ownership pending an explicitly verified hardware shutdown. Carrier
and PCI/controller readiness are also rechecked under each ring's producer
lock. The selector remains behind `KUROGANE_EXPERIMENTAL_RTL81XX`.

The production-path MMIO/DMA fixture injects address and ring-wrap corruption
for both RX and TX, checking unchanged buffers, counters, ring positions and
doorbells. The same host tests are included in the UBSan runner. These
injections do not demonstrate physical DMA recovery, device reset, or
hardware compatibility. **Realtek remains BLOCKED — PHYSICAL HARDWARE
REQUIRED.** Confirm these changes against the final source SHA and CI run;
do not inherit PASS from the previous candidate.

## 2026-11-24 — review of the device matrix

The push and PR runs for `55f7857ce89979a3aac9c632b53641f9973a679b` passed.
The original release ISO in the push run had SHA-256
`e58618da5a3fa8f0d92836af80ea5c877351a5bfbe9f465fa5251ff33e5eace1`.
The artifact records results for direct USB storage, downstream boot-time
storage with restoration of the scratch disk, HDA PCM and AC’97 fallback
PCM. It records separate image hashes and patches for the instrumented
power, root-port hotplug and xHCI retirement trials.
[Push run](https://github.com/KrzysioYT/KuroganeOS/actions/runs/44559586826) ·
[Gate evidence](https://github.com/KrzysioYT/KuroganeOS/actions/runs/44559586826/artifacts/14550155815)

The closeout gate now also includes NVMe Identify and namespace reads. The
shared block-device test writes, flushes, reads back and restores eight
sectors of a dedicated namespace with a validated scratch header. QEMU
checks the SHA-256 of the entire raw disk before and after the guest runs.
A separate boot verifies that the legacy NVMe marker leaves every sector
unchanged. The gate boots with an untagged 4 KiB namespace and verifies that
the raw disk stays unchanged. It also installs from ISO to a fresh AHCI disk
and boots twice from that disk alone. The downloadable test ISO is retained
before the workflow rebuilds the instrumented images.

NVMe shutdown and startup rollback now retain DMA pages and mappings if
controller reset cannot be verified. The driver attempts to disable bus
mastering while keeping ownership of resources that may still be used by
the hardware. The host and UBSan tests cover reset timeout, fatal status,
PCI identity loss, partial mapping and retry after a failed release.
At runtime, NVMe serializes submissions and keeps the shared bounce buffer
untouched after a timeout or mismatched completion until verified teardown.
The same fixture checks that a matching completion with a command error
still permits the next request.

When a companion hub probe cannot verify the state of the port or its
transfers, xHCI stops polling and retains the active slot and its DMA
allocations. An unreadable port register on an active companion likewise
contains the controller before it can process a pending primary-device
completion. Both paths have optimized and UBSan host regressions.
When Reset, GetStatus or ClearResetChange fails during a hub port reset,
the driver stops the EP0 sequence and contains the controller. A timer or
port-state abort after acknowledged transfers still uses verified disable.

The experimental Realtek path verifies live PCI vendor and device identity
around changes to the PCI Command register and reports RX/TX faults through
an atomic flag. Host/UBSan cover identity loss at start and a device fault
reported by the runtime RX/TX paths.

A Windows host reports an RTL8168 with PCI ID `10ec:8168`, subsystem
`17aa:39dc` and revision `15`. Its KuroganeOS runtime result remains
**BLOCKED** pending a controlled physical boot of an experimental build.
The test must record the build and image hash, MAC/PHY and link state,
bidirectional traffic and link recovery, plus whether the driver can verify
DMA quiescence before releasing resources. The default ISO is suitable for
checking PCI recognition and fallback. Its experimental RTL81xx path stays
disabled. Persistent downstream USB binding and hotplug, high-speed hub TT
scheduling and active-driver PCI power recovery also remain open.
