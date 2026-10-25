# Flux Desktop — loss-aware Ring-3 input queue

> Zapis historyczny / dowód dla wskazanej rewizji. Nie jest instrukcją
> bieżącego wydania. Aktualny stan: [DEVELOPMENT_STATE.md](../../DEVELOPMENT_STATE.md).

2026-10-24. Scoped UI input-delivery milestone, not full 8.0.0.

## Bug
The production Ring-3 UI per-process queue had 16 slots (15 usable).
On saturation it blindly discarded the oldest event. Rapid USB pointer
movement could evict a queued keyboard event, left-button press or release.

## Implementation
The allocation-free `kernel/user/ui_event_queue.hpp` now owns the
queue policy shared by production and host regression:
- Consecutive pointer positions with identical pressed-button masks merge
  into the latest sample; a transition is never coalesced away.
- Consecutive same-sign scroll events sum with signed saturation.
- A full queue can retire the oldest pointer position only if a later
  pointer of the same button state supersedes it, with no intervening
  different button state; interleaved key events keep their FIFO order.
- If no redundant motion exists, the incoming event is rejected and
  a per-queue dropped counter increases, preserving already queued
  keyboard and button edges rather than silently erasing them.
- Pop operations respect 16-slot wraparound; input callbacks reject a
  stale window ID that no longer belongs to their process context.

## Qualification and limits
`tests/test_ui_event_queue.cpp` covers 600-sample motion storms,
press/move/release ordering, scroll polarity and accumulation, full
key-event saturation, redundant-pointer reclamation and 256 wraps.
Optimized and UBSan runs are in the exact-SHA Flux foundation gate
alongside host UI and real QEMU USB-only Ring-3 login.

A finite queue cannot promise lossless transport under unbounded input;
this change prevents a *motion storm* from displacing existing click/key
events. UI processing remains serialized by the existing kernel context.
No claim is made of concurrent MPMC safety or complete 8.0 release.

The standard `scripts/run-host-tests.sh` suite also compiles and executes
the production queue tests (without sanitizers); the focused 8.0 qualification
adds UBSan and validates the same production Ring-3 path in a release image.
