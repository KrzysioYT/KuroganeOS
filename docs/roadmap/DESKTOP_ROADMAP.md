# Desktop roadmap

1. Harden panic/logging and add deterministic exception tests.
2. Add page tables, GDT/TSS, ring 3, processes, threads, syscall ABI and IPC.
3. Add a block API, AHCI, GPT, VFS and a persistent filesystem.
4. Move applications and desktop services to user mode; add a compositor.
5. Add input events, virtio/e1000 networking, security identities and audio.
6. Add package/update transactions and recovery.
7. Implement one target-side installer core shared by interactive and answer
   file front ends.
8. Test install, disk boot, first boot, persistence and recovery in QEMU.

An installer before steps 2–3 would be an unsafe façade, so it is deliberately
not represented as complete.
