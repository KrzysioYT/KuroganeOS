# KuroganeOS Road to 15 status

This file is generated only from qualified development evidence.

## 3.6 Flux Stabilization

Qualification must remain tied to the exact same source SHA through the dedicated closeout workflow. Do not mark this milestone qualified unless host/release, Flux Runtime Core, Flux Session Recovery, the 3.4 regression sweep and the 3.5 Connected Userspace closeout all succeed for that SHA.

## 4.0 Pre-Steel / KuroFS 1.0

Active work starts with persistent KuroFS allocation primitives on top of the production `storage::block::Device` contract. The first slice is limited to safe persistent allocation of contiguous data extents and inode records with remount persistence, metadata protection, bounded scanning and explicit no-space behavior. File/directory mutation and reclamation follow only after allocator durability is proven.
