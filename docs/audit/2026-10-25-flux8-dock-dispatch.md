# Flux 8 runtime blocker: Dock recentering — 2026-10-25

> Zapis historyczny / dowód dla wskazanej rewizji. Nie jest instrukcją
> bieżącego wydania. Aktualny stan: [DEVELOPMENT_STATE.md](../../DEVELOPMENT_STATE.md).

Run 40141189698 at 621bb56 passed the optimized/UBSan gate but failed the
live desktop gate. Serial evidence confirms real Login, account lookup, session
creation, Home first frame, Files launch and Files wheel paging. Terminal never
launched: the second click delivered a mouse button but no Dock shortcut.

The Dock width grows by a task entry after Files opens, shifting its centered
origin. The old normalized Terminal x=410 belonged to the no-task layout.
The qualified smoke now uses x=367 after Files is ready. A host test exercises
production window creation and pointer dispatch with the same double integer
normalization as QMP and input, at 1024x768 and 1280x800. It actually receives
'f', creates Files, then requires 't' from Home's callback. The old x=410 fails
this test; the replacement passes optimized and UBSan builds. Both real process
spawn and surface-ready counts of two remain mandatory in QEMU.

This repairs a test targeting error, not the unresolved installed VirtualBox
service restart issue. It does not bypass credentials, weaken application-ready
criteria or declare complete 8.0. Runtime CI must confirm the new candidate;
VirtualBox installed-root diagnostics and remaining Modern contract items stay
open until independently verified.
