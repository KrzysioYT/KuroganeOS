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
| USB hubs, multi-device hotplug, wider HID | Enumeration through a hub, detach/re-attach, device churn, simultaneous keyboard/mouse and negative report validation | **DOWNSTREAM RESET / NOT QUALIFIED** — at controller start, xHCI can debounce and reset one powered, disabled downstream port, verify the reset event and disable the port again before retiring the hub slot. CI requires these responses from port 1 and direct keyboard/mouse input. Downstream addressing, TT scheduling and hub hotplug remain open. Physical hub qualification is blocked without suitable hardware. |
| Device power management | PCI D-state lifecycle, PMCSR/PME and device-specific safe recovery; system-wide suspend/resume only if part of the accepted 7.0 contract | **PARTIAL** — the PR gate boots a separately instrumented image from the exact candidate SHA and requires four pre-bind D0 → D3hot → D0 cycles followed by E1000e DHCP and ICMP. PMCSR transactions check PCI identity and preserve unrelated live controls. Active-driver recovery and physical power transitions remain unverified. |
| Error isolation and hardware inventory | Unsupported hardware explained without claiming functional driver; optional-device failure does not stop boot or damage storage | Needs end-to-end evidence |
| Closeout regression | Full host, UBSan, kernel tests, clean IMG/ISO, real QEMU/OVMF with device-matrix logs, checksums and exact SHA | **OPEN** |

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
