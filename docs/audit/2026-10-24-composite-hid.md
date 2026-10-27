# Iron Shield 7.0: composite USB HID pointer reports

> Zapis historyczny / dowód dla wskazanej rewizji. Nie jest instrukcją
> bieżącego wydania. Aktualny stan: [DEVELOPMENT_STATE.md](../../DEVELOPMENT_STATE.md).

The previous pointer descriptor parser refused all interfaces with more than
one HID Report ID, even when the extra reports belonged to unrelated keyboard or
vendor functions.

This change maintains independent input bit offsets for each Report ID, scopes
pointer decoding to mouse/pointer application collections, and selects one
pointer report without including bytes from other IDs. It also distinguishes
the selected pointer payload length from the largest interface transfer length
so xHCI allocates enough of its bounded interrupt packet to receive all IDs.
Unsupported reports are ignored without changing the pointer button state.

Host regression covers a composite keyboard+mouse+vendor interface, payload
lengths, negative/truncated reports, button release, original relative pointer
and absolute tablet compatibility. Scope remains bounded: one pointer layout
per interface, fixed-size report table and no general multi-touch mapping. The
release kernel and QEMU USB input qualification must pass before calling this a
qualified 7.0 device feature.

Version remains 7.0.0-dev; no 15.0 stable claim.

## Follow-up: HID Global PUSH/POP

Composite descriptors can PUSH global attributes before a vendor report and POP
them afterward. A bounded 8-entry stack now restores usage page, logical
limits, report size/count and current ID without rewinding per-ID input bit
positions. Malformed underflow, overflow and unbalanced descriptor state are
rejected. This remains intentionally narrower than a full general-purpose
HID parser.
