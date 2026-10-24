# Flux Desktop: Ring-3 wheel delivery and Files paging

Date: 2026-10-24. Scoped 8.0 desktop input engineering candidate.

## Defect
Window Manager already had native MouseWheel events, but the Ring-3
`input_user_window` bridge discarded them. Applications could not react
to a real USB mouse wheel even when the controller and compositor worked.

## Changes
- Public `KU_UI_EVENT_SCROLL=4` extends existing UI event types without
  changing the `KU_UI_ABI_VERSION=1` contract or 32-byte event structure.
- The former 32-bit `reserved` slot is now a C11 anonymous union with
  `int32_t wheel_y`. Old code still has `reserved`, layout and alignment
  are unchanged. A positive wheel_y means upward scrolling.
- The kernel maps native MouseWheel to content-relative
  `KU_UI_EVENT_SCROLL` for the pointer-hit-tested Ring-3 application.
- Files Explorer uses one wheel detent per six-item directory page:
  down moves forward, up moves backward, and page limits do not wrap.
  The selection is cleared only when a page actually changes.
- Optimized and UBSan host tests check event ABI layout, signed delta,
  and scene scrolling; the same-SHA QEMU release build checks kernel and
  userspace compilation and USB login.

## Limits
Physical wheel movement is not yet injected during QEMU qualification.
Other GUI applications may need explicit handling of KU_UI_EVENT_SCROLL.
No 8.0 or 15.0 stable-release claim is made from this checkpoint.
