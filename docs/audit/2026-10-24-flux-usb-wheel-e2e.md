# Flux Desktop: USB wheel through QEMU to the Ring-3 Files Explorer

2026-10-24. Scoped integration milestone candidate; qualify only if exact-SHA
host regressions and QEMU USB-only live desktop both pass.

The native wheel path is xHCI USB HID -> kernel input -> window hit testing
-> bounded Ring-3 UI queue -> public KU_UI_EVENT_SCROLL -> Files Explorer.
The QMP smoke command now accepts --wheel-after-marker TEXT X Y up|down,
moves the selected USB mouse to the requested guest coordinates and injects
a USB wheel event after the marker appears. The Flux QEMU run clicks the
Files dock pin after the desktop is ready, waits for the Files VFS/grid
marker, scrolls, and requires a marker which Files emits **only after a real
Ring-3 wheel event changes the visible page**.

The pager honors signed multi-detent deltas merged by the bounded queue,
saturates at the first/last tile page, handles INT32_MIN and partial pages.
The tested C11 paging helper is used by the production Files app. Tests run
in the full host suite and with optimized/UBSan in the Flux gate.

The QEMU probe uses the desktop's 1024x768 dock layout and is not a
physical-hardware qualification; a future display-mode change may require
adjusting the click coordinates. Not a full 8.0.0 or 15.0.0 release.

QEMU launch sequencing was verified against the preceding qualified serial
artifact: launcher Ring-3 starts before the login-supervision marker. The
Files dock click is deferred until `red_flux_login_to_desktop: PASS` to avoid
interacting with the still-transitioning login window.
