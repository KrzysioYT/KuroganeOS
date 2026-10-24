# PCI PME recovery in same-state restores — 2026-10-24

Milestone: Iron Shield 7.0.0-dev (not a stable release).

## Defect

When a PCI Power Management transaction began and ended in the same D-state,
`change_state()` returned early without reconciling PMCSR's PME Enable bit.
If device firmware or another serialized owner changed PME Enable while the
restore token was held, `restore()` released the transaction as successful
while the original wake configuration remained altered.

## Fix

- Reconcile PME Enable on restore even if the D-state did not change.
- Preserve the existing bus-master/DMA quiesce rule when changing D-states.
- Continue masking the PMCSR PME Status write-one-to-clear bit on every write.
- Verify the resulting D-state **and** PME Enable before releasing ownership.
- Keep the transaction active on verification failure so callers may retry.

## Regression

The host PCI power suite exercises D0 and D3hot same-state restores after PME
Enable changes, first with deliberately ignored PMCSR writes and then a
successful retry. It also asserts no settling delay for same-state control
writes, retained W1C event status, and no I/O for the initial no-op
transition. The exact-commit 7.0 Intel GbE and device-foundation GitHub
Actions workflows must pass before this is qualified.

This fixes a control-register lifecycle gap; it does not establish physical
suspend/resume support or complete the 7.0 driver-coverage gate.
