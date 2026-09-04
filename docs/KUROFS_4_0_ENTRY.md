# KuroFS 4.0 entry

The 4.0 Pre-Steel storage work begins only after the persistent allocator slice passes the production host regression suite and release kernel build on the self-hosted KVM runner. The allocator must use the production `storage::block::Device` contract and survive remount without overlapping extents or inode reuse.

## Current consistency model

- Regular-file and directory replacements become durable before inode publication.
- Cross-directory move uses the `FEATURE_MOVE_INTENT` superblock feature. Existing v1 volumes are upgraded by a higher-generation redundant-superblock publication before their first cross-parent move.
- The move intent records both parent snapshots and replacement extents. Destination publication precedes source removal; recovery aborts a wholly unpublished intent or completes a partial publication before mount succeeds.
- Clearing the intent precedes reclamation of superseded extents. An interrupted cleanup may leak bounded space, but cannot transfer a live block to a second inode.
- Mount validates every live inode extent, rejects overlapping live ownership, validates all directory record CRC/generation/type bindings, rejects duplicate names or child IDs and rejects cross-parent aliases or directory ancestry cycles.
- `FEATURE_INODE_OWNERSHIP` gives allocated inode slots explicit `PENDING` and `ORPHAN` states. A normal namespace publication transitions its child to `LIVE`; mount completes an interrupted attach or converts an unattached pending inode into an orphan.
- `FREE` zero slots and generation-carrying `TOMBSTONED` slots remain distinguishable. An orphan directory is the unattached root of a detached subtree; its descendants retain their unique local parent ownership.
- Explicit bounded reclamation tombstones regular-file and empty-directory orphans before releasing their owned extents. Every interrupted write/flush phase remounts to either the original orphan or its generation-carrying tombstone; a post-tombstone interruption may conservatively leak blocks but cannot create duplicate ownership.
- Non-empty orphan directory trees are reported and deferred until recursive reclamation has its own durable intent. Low-level unattached block reservations remain legal, are not attributed to an inode owner and are never consumed by orphan reclamation.

Host coverage injects one failure at every persistent write and flush in both file and non-empty-directory moves, remounts the resulting image and requires exactly one old-or-new namespace owner with non-overlapping live extents.

Regular-file copy-on-write recovery is also interrupted at every data, bitmap, inode-publication and cleanup write/flush. Once inode publication begins, an ambiguous failure conservatively preserves both old and replacement allocations so remount can expose one complete old-or-new payload without ever freeing its live extent.

Native raw-volume persistence passed on source SHA `6dd9581e79d79bcd5155b4aa719d7ffcf1a1f8b1` in Actions run `33817447611`. A clean release kernel mounted a dedicated AHCI KuroFS disk at `/kuro`; the Ring-3 probe used the public filesystem ABI to create directories, write and sync a file, move it across parents and validate the new namespace. A second, new OVMF/Q35/KVM process reused the same unformatted image and read the exact payload back. The formal same-SHA 4.0 closeout remains pending.
