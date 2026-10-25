# Compositor redraw lifecycle — 2026-10-23

> Zapis historyczny / dowód dla wskazanej rewizji. Nie jest instrukcją
> bieżącego wydania. Aktualny stan: [DEVELOPMENT_STATE.md](../../DEVELOPMENT_STATE.md).

This is an incremental Flux Desktop Platform hardening change, not an 8.0 or
15.0 qualification claim.

`render_if_needed()` previously cleared all damage after invoking window draw
callbacks. A callback requesting a partial or full redraw therefore lost that
request at the end of the current frame. Consume the pending damage snapshot
before drawing, preserving later invalidations for the following frame. Reject
nested render calls so callbacks cannot recursively consume the new damage or
enter the framebuffer while a frame is active. This is a reentrancy guard under
the existing compositor execution model, not cross-CPU synchronization.

The host renderer now invokes draw callbacks without framebuffer operations.
Regression cases cover partial invalidation during full redraw, full invalidation
during partial redraw, recursive render rejection and eventual idle after the
next frame. Reintroducing the old end-of-frame clearing causes the new regression
to fail with exit 106; the corrected implementation passes both optimized and
UBSan builds. The release kernel builds successfully.

A focused GitHub Actions workflow runs optimized and UBSan window-manager tests
on matching integration PRs. Host callback tests do not qualify pixel rendering,
physical display output or all of the 8.0 desktop scope. Runtime qualification of
the preceding PS/2 and harness fixes was still in progress during this change.
