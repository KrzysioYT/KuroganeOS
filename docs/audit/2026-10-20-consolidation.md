# Road-to-15 consolidation — 2026-10-20

> Zapis historyczny / dowód dla wskazanej rewizji. Nie jest instrukcją
> bieżącego wydania. Aktualny stan: [DEVELOPMENT_STATE.md](../../DEVELOPMENT_STATE.md).

## Changes

Merged the divergent 7.0 source lines: `chatgpt/7.0-iron-shield` at
`11f426c` and `chatgpt/7.0-driver-device-expansion` at `5ebb515`.
Retained the newer timed/recoverable PCI PM implementation and a single Intel
GbE classifier. Restored modern VirtIO block, shared VirtIO PCI transport,
generic signed scratch-media checks and the VirtIO runtime smoke profile.

Additional hardening:
- reject capability lengths extending beyond 256-byte PCI configuration space;
- reject unknown block opcodes and flush without negotiated support;
- validate complete transfer ranges before copying or issuing any chunks;
- validate modern split-ring completion ID, written length and device status;
- reset after timeout/invalid completion; retain DMA pages if reset fails;
- link the shared transport in the existing VirtIO-net cleanup regression;
- preserve both exact-candidate CI workflows, fixing obsolete API probes;
- remove contradictory duplicate 6.0 status and align milestone table names.

Completion length follows the modern split-ring contract in OASIS VIRTIO 1.2,
sections 2.7.8 and 5.2.6:
https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html
The driver negotiates the modern interface; this is not a legacy-length rule.

## Validation

- Release kernel ELF compilation/link: PASS locally.
- `scripts/test.sh`: PASS locally (kernel/core, process/thread/IPC, SDK tests).
- Full `scripts/run-host-tests.sh`: BLOCKED locally by AF_UNIX socket creation
  denied in `tests/test_usb_hotplug_qmp.py`. The repository runner is unchanged
  with respect to this mandatory test; CI must run it.
- Remaining host suite: PASS locally with only the blocked QMP test
  excluded in a temporary diagnostic runner, not a release qualification.
- Full IMG/ISO build and QEMU runtime: NOT RUN. This host lacks gdisk, mtools,
  dosfstools, xorriso, QEMU and OVMF; package installation found no candidates.
- PCI PM cycles and VirtIO/net coexistence after integration: require CI.
  The block workflow now covers both E1000e + VirtIO block and simultaneous
  VirtIO-net + VirtIO block. MMIO separation regression passes locally.

## Remaining scope

7.0 remains unqualified. Broader Realtek coverage, USB hubs and full driver
suspend/resume are still open. The VirtIO block driver remains a bounded,
single-controller polling implementation; broader concurrency, hotplug and
reset failure injection require additional qualification. Protocol unit tests
are not hardware or DMA-runtime evidence.

Milestones 8–13 and the release/install/upgrade/recovery/security/stress and
physical-machine matrices in 14–15 remain open. No product-stable version is
claimed, and no version number was advanced by this consolidation.

## Follow-up: controller ownership and failure injection

Added nonblocking atomic ownership around complete read/write/flush callbacks
and initialize/shutdown. The lock is outside the resettable controller state;
contenders return DeviceBusy rather than spinning on a preempted I/O owner.
Ownership includes the bounce-buffer copy before submission and after read
completion. Shutdown now returns a status so a busy caller can retry.

A production-driver host fixture exercises four concurrent contenders, full
range rejection before writes, actual polling timeout followed by reset,
unsolicited completion cleanup, DMA quarantine after failed reset, failed page
release and PCI-command restoration retry. It found and fixes an early-return
bug which discarded pending DMA cleanup after MMIO had already been unmapped.
The fixture passes locally; the release kernel compiles. It uses bounded host
MMIO/DMA adapters and does not claim physical-device failure qualification.
