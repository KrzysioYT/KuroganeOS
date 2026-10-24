# Iron Shield 7.0 supported-device combined qualification — 2026-10-24

## Goal

Make a substantial, same-source-SHA qualification of the supported Iron Shield
driver subset. Independent earlier Steel and E1000e/VirtIO qualification jobs
were green but did not force xHCI USB keyboard/mouse, network, modern VirtIO
block, storage write/readback and SMP to run simultaneously in one boot.

New workflow `qualify-7.0-supported-devices.yml` runs a fresh release image and
kernel/SDK/host regressions, then two multi-controller runtime profiles:
E1000e+VirtIO block+USB HID+SMP and VirtIO-net+VirtIO block+USB HID+SMP.
It also checks a USB-only desktop with no NIC and PS/2 disabled.

The final job requires every matrix case and the NIC-less fallback to pass.
QEMU logs remain attached to the same Actions SHA. The no-NIC case is not
a network pass; it is only an input/desktop degradation smoke.

## Explicit non-goals

This qualifies the *currently implemented supported subset* only, and
must not be relabeled as complete Iron Shield 7.0.0. Realtek RTL8111/8168
support, hub topology/hotplug breadth, actual system-wide suspend/resume
and physical hardware acceptance remain open. Tests of a virtual USB mouse
also do not prove that every composite HID vendor implementation works.

The dev version remains `7.0.0-dev`; 8.0/15.0 are not claimed.
