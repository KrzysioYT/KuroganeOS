# KuroganeOS filesystem policy — ABI v1

Target: KuroganeOS 3.3.x / public ABI major 1.

## Link semantics

Public filesystem ABI v1 intentionally does **not** expose symbolic links or
hard links.

This is a deliberate compatibility and security decision rather than an
accidental missing syscall:

- the persistent root filesystem is FAT32, which has no native Unix symlink or
  inode hard-link semantics;
- encoding links as magic ordinary files would make path traversal depend on a
  private convention and would be ambiguous to external FAT32 tools;
- emulated hard links would require a separate durable object/inode layer to
  preserve reference counts and atomic unlink semantics;
- transparent symlink traversal would expand the attack surface of chroot,
  future per-user roots, installers and service sandboxes before those policy
  boundaries are finalized.

Therefore ABI v1 path resolution treats every FAT32 directory entry according
to its real backend node type and never interprets file contents as a link.
Applications must not depend on symlink or hard-link creation, traversal or
metadata.

## Future compatibility

A future filesystem/ABI revision may add links only when KuroganeOS has a
backend with durable inode/link semantics and the following behavior is defined
and tested together:

1. maximum traversal depth and loop detection;
2. absolute versus relative symlink target resolution;
3. interaction with process cwd/chroot and future sandbox roots;
4. rename/unlink atomicity and hard-link reference counting;
5. `stat` versus `lstat`-style metadata semantics;
6. installer/recovery behavior and compatibility with offline tooling;
7. permission checks at every traversed component.

Until then, the absence of links is part of the stable ABI v1 contract.
