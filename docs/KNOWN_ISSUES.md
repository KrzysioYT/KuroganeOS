# Znane ograniczenia

Stan na 14 sierpnia 2026 r.

- Root shell używa RAMFS; pliki nie przetrwają restartu.
- Root FAT32 jest montowany w runtime tylko do odczytu; kernel shell nadal używa RAMFS. Brak mutacji, block cache i trwałego `sync`.
- AHCI jest zweryfikowane na QEMU q35, nie na szerokiej macierzy fizycznego sprzętu.
- GPT sprawdza primary header; pełny recovery z backup GPT i MBR fallback pozostaje do wykonania.
- PMM zarządza jednym regionem; kernel dziedziczy część mapowania UEFI i nie ma pełnego W^X/NX.
- Kernel jest UP i używa legacy PIC/PIT; brak APIC/IOAPIC, HPET i SMP.
- Scheduler callbacków nie przełącza kontekstów; brak procesów, ring 3, syscalls, FD i IPC.
- Brak KuroLibC runtime, KuroPOSIX, init i Service Managera.
- Network PASS oznacza tylko protokoły/loopback; brak NIC i sieci zewnętrznej.
- Brak USB/xHCI, ACPI poweroff, `/dev`, `/proc`, `dmesg` ring buffer i recovery filesystemu.
- GUI jest `EXPERIMENTAL`, działa w kernelu i pozostaje zamrożone.
- Nie ma kwalifikacji realnego hardware ani długotrwałych testów soak/SMP.

Aktualne dowody testowe znajdują się w [FOUNDATION_PROGRESS.md](FOUNDATION_PROGRESS.md).
