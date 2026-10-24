# Flux Desktop 8.0: stable window enumeration and live-session gate
2026-10-24 — Scoped engineering qualification, not the 8.0.0 release.

## Defect and fix
`windowing::list()` iterated g_order and g_count directly while invoking
application callbacks. A callback that closed or created a window could reuse
an enumerated slot and change z-order, skipping a surviving application or
including a newly created one in place of a removed window.

The fixed enumeration copies up to MAX_WINDOWS metadata snapshots before
invoking callbacks. Each item is rechecked by generation-safe WindowId before
delivery, so dead instances are omitted and replacement instances appear only
in the next enumeration. Metadata for surviving windows remains a consistent
start-of-list snapshot even if callback changes focus and z-order.
`IterationStopped` continues to stop immediately.

## Qualification contract
The scoped 8.0 CI job runs optimized and UBSan regressions for Window Manager,
viewport/pointer input and the production clipboard service. A separate
exact-SHA QEMU job builds a fresh image, boots with no PS/2 and no NIC, attaches
USB keyboard and mouse, logs into the Ring-3 desktop and requires launcher,
keyboard and mouse runtime markers. The closing job requires both passes.

Do not claim hardware mode switching, drag/drop, complete notification and
settings UI, physical compatibility, or 8.0.0 stable based on this gate.
