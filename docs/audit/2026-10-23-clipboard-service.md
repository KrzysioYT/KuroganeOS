# Clipboard response ownership and Unicode — 2026-10-23

> Zapis historyczny / dowód dla wskazanej rewizji. Nie jest instrukcją
> bieżącego wydania. Aktualny stan: [DEVELOPMENT_STATE.md](../../DEVELOPMENT_STATE.md).

The clipboard daemon previously discarded the return status of IPC sends.
When the client's inbound queue was full, SET/CLEAR could mutate the clipboard
without delivering the response. The daemon would then receive another request,
losing request/response ordering and leaving clients unable to observe completion.

Each of the eight bounded client slots now retains one complete response snapshot.
WOULD_BLOCK retries that snapshot without receiving another request or replaying
its mutation. Other clients continue to progress. A successful send clears the
pending snapshot; a terminal transport error closes and clears the client slot.
This guarantees no replay while a connection is alive; it is not a durable
exactly-once transaction guarantee across disconnect or service restart.

UTF-8 text now requires valid Unicode scalar encoding, a final NUL counted in
`data_size`, and no embedded NUL. Overlong encodings, surrogate code points,
truncated continuations and values above U+10FFFF are rejected without changing
clipboard state. Binary payloads remain unrestricted within the 192-byte limit.
The wire structures, operation IDs and payload capacity are unchanged.

The host test includes the production daemon and substitutes only IPC transport
calls. It exercises response backpressure, two-client progress, immutable response
snapshots, ordered SET/CLEAR, terminal disconnect cleanup, generation wrap,
Polish text, emoji, malformed Unicode, binary capacity and malformed envelopes.
Optimized and UBSan runs pass. The complete release Linux build (kernel, userspace,
SDK and EFI loader, without disk image) passes. Focused CI and the existing host
suite run this regression. All locally available host regressions pass. The USB QMP socket test remains
unavailable locally because AF_UNIX creation is denied; it is not skipped in CI.
Real kernel IPC/runtime qualification remains separate.

Stable release remains blocked on the uncompleted roadmap scope, including full
8.0 desktop qualification, the 9.0 network/TLS 1.3 scope, VFS/application/security
and signed-update/recovery work, performance/power qualification, and the 14.0
multi-hypervisor/physical Intel/AMD installation, upgrade and recovery matrix.

## Integration and runtime gate — 2026-10-24

Integrated on top of `faceb4526acbbee3bc3ab544d7af76f12132766d`, preserving
the newer HID, PCI/VirtIO, paint iteration and supported-device matrix changes.
All locally available host regressions and the full release no-image build pass
on that combined source. The historical clipboard workflow has been replaced
with a read-only exact-candidate qualification: it launches and reaps the existing
probe before login, checks Unicode/invalid encoding over real kernel IPC, and
requires service liveness and the login surface. Probe and injected init compile
with freestanding release flags. No QEMU pass is claimed until CI completes.
