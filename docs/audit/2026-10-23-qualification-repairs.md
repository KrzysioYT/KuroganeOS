# Qualification repairs — 2026-10-23

CI for `d1c03fa25e93` passed both 7.0 device qualification workflows, including
Intel GbE and VirtIO coverage. Five older qualification jobs failed:

- Steel Closeout (39442676229): a fixed `5.0.0-dev` assertion rejected 7.0.
  Validate the canonical version, minimum foundation major and release-note
  consistency instead. Retain the runtime cross-CPU-work requirement; the old
  blanket ban on a scheduler marker is obsolete after Scheduler 2.0.
- Universal Input (39442676581) and Hardware Compatibility (39442676455):
  QEMU reported `QEMU HID Tablet`; the click helper only accepted `USB Tablet`.
  Accept both names, retaining device-kind selection and QMP acknowledgements.
- Universal Storage (39442676435): the required `nvme_namespace_read` marker
  does not exist. Require the production `nvme_read: PASS`, as the dedicated
  NVMe qualification already does. USB enumeration/read and NVMe identify
  requirements remain intact.
- Hardware Portability (39442676442): with i8042 disabled, keyboard initialize
  returned false but `initialized()` returned true because IRQ unmasking had
  succeeded. Require controller configuration before unmasking and publishing
  readiness; unregister the handler on failure. Preserve USB-only SKIP gates.

Regression coverage adds a host I/O fixture exercising the actual keyboard
initialization/shutdown implementation (absent device, successful setup,
IRQ failure and retry), plus both QEMU tablet/mouse naming variants and
wrong-device rejection against the click helper's actual selector.

Local validation: release kernel build passes; shell syntax and diff whitespace
checks pass. Host tests before the QMP socket test pass. This environment denies
AF_UNIX socket creation, so the QMP test cannot run locally and remains mandatory
in CI. All tests after that boundary pass when run separately, including the TLS
freestanding probe, SDK build and project-generator integration. These changes require a new
exact-commit QEMU qualification; neither 7.0 stable nor 15.0 stable is claimed.
