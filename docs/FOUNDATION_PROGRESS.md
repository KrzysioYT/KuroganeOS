# KuroganeOS Foundation Progress

Data statusu: 14 sierpnia 2026 r.

## Current milestone

Milestone 3 — Storage (w toku). Milestone 0, 1 i 2 mają wykonane wymagane implementacje i testy. Milestone 3 nie jest ukończony, dopóki VFS/FAT32 nie zapisze pliku i test dwóch bootów nie potwierdzi trwałości.

## Working

- UEFI boot, loader ELF64 PIE, protokół startowy v2 i domyślny Console Mode.
- Profile `debug`, `release` i `test`, centralne `config/features.conf`, manifest buildu, IMG, ISO i logi regresji.
- `libk`: status/error API, memory/string/format, kontenery, hash, CRC32, UTF-8, math, atomics, logging i assert.
- GDT/TSS/IST, IDT, wyjątki, PIC/PIT, RTC, PS/2, prywatny PML4 i runtime test mapowania.
- Device Manager i Driver Manager: rejestracja, relacje parent/child, ownership, matching według priorytetu, probe/attach, fallback oraz timeout budget.
- PCI rejestruje rzeczywiste urządzenia w Device Managerze.
- AHCI dla QEMU q35: wykrywanie, `IDENTIFY`, read, write, flush i bounded polling.
- GPT czytane z prawdziwego dysku AHCI; dwa urządzenia blokowe są publikowane jako dzieci kontrolera.
- Polecenia `device list`, `device info`, `driver list`, `driver info` i `diskinfo` pokazują dane runtime.
- RAMFS i kernel shell z rzeczywistymi operacjami na plikach.
- Hostowe testy libk, driver core, memory, VMM, RAMFS, block/GPT, VFS/FAT32, schedulera, network i SDK.

## Partial

- Storage: sprzętowy block I/O i GPT działają, ale block cache i montowanie root FAT32 przez kernel nie istnieją.
- Root partition jest montowana read-only przez `PartitionDevice → FAT32 → VFS`; kernel odczytuje `/etc/system.conf`. Nie jest jeszcze zapisywalnym rootem shella.
- PMM obsługuje jeden wybrany region fizyczny; VMM dziedziczy niższe mapowania UEFI.
- Scheduler uruchamia callbacki na wspólnym stosie kernela; nie przełącza kontekstów wątków.
- Network obsługuje protokoły i loopback, ale nie ma sterownika NIC.
- SDK ma nagłówki i test compile-only, bez działającego transportu syscall.
- Safe mode ogranicza sterowniki i sieć, ale nie naprawia jeszcze filesystemu.

## Failed / not implemented

- Persistence test boot #1 / boot #2.
- Block cache, `sync`, `/dev` i `/proc`.
- ACPI core, APIC/IOAPIC, HPET i SMP.
- xHCI, USB Core, USB HID i USB Mass Storage.
- Procesy, wątki z context switch, ring 3, syscalle, FD i IPC.
- KuroLibC runtime, KuroPOSIX, `init`, Service Manager i userspace shell.
- Fizyczny NIC, DHCP, TCP, DNS i sockets.
- Boot Health, pełne Recovery i aktualizacje A/B.

## Tests

- clean `debug`, `test`, `release`: **PASS** w końcowej macierzy 2026-08-14.
- freestanding PIE, 289 dozwolonych relokacji i brak RWE: **PASS**.
- wszystkie host unit tests, w tym `libk` i `driver-core`: **PASS**.
- QEMU normal mode: Device Manager = 7, Driver Manager = 1, AHCI disks = 2: **PASS**.
- QEMU AHCI tagged scratch `write → flush → readback → restore`: **PASS**.
- QEMU GPT z rzeczywistego dysku AHCI, 2 partycje: **PASS**.
- QEMU safe mode: PCI = 0, shell i wymagane self-testy: **PASS**.
- release ISO i ISO ShellTest po integracji: **PASS**.
- VFS/FAT32 host tests: **PASS**.
- Generated base IMG `GPT → PartitionDevice → FAT32 → VFS → /etc/system.conf`: **PASS** na hoście.
- QEMU runtime FAT32/VFS read z AHCI, label `KURO_ROOT`, 51 bajtów konfiguracji: **PASS**.
- FAT32 write i persistence: **NOT IMPLEMENTED**.
- ring 3/syscall/process tests: **NOT IMPLEMENTED**.

## Current blocker

Ścieżka od AHCI do read-only VFS działa. Blokadą Milestone 3 jest bezpieczny FAT32 write: alokacja/zwalnianie klastrów, aktualizacja obu FAT i katalogów, failure atomicity, block cache oraz `sync`.

## Next task

Rozszerzyć FAT32 backend o testowane create/write/rename/delete, następnie dodać block cache, `sync` i automatyczny test trwałości na dwóch bootach. Kernel shell pozostaje na RAMFS, dopóki write path nie będzie bezpieczny.

Szczegółowy stan i granice dowodów opisuje [FOUNDATION_AUDIT.md](FOUNDATION_AUDIT.md).
