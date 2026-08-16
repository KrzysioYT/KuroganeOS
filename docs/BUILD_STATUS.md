# Build status

Data: 14 sierpnia 2026 r.

## Current stage

KuroganeOS bootuje jako x86-64 UEFI Console Mode i ma zweryfikowany framework urządzeń oraz pierwszy prawdziwy sterownik storage. Bieżący etap to połączenie działającego AHCI/GPT z VFS i trwałym FAT32.

## Working

- profile `debug`, `release`, `test` i jeden kanoniczny frontend `scripts/build.ps1`;
- centralne feature flags i `build/build-info.txt`;
- walidowany ELF64 PIE, własny loader PE32+, deterministyczny FAT IMG i GPT base IMG;
- host unit tests oraz automatyczne testy QEMU z markerami PASS/FAIL i timeoutem;
- `libk`, Device Manager, Driver Manager, PCI oraz AHCI read/write/flush;
- odczyt GPT przez prawdziwy kontroler AHCI w QEMU;
- read-only mount `KURO_ROOT` przez PartitionDevice/FAT32/VFS i odczyt `/etc/system.conf`;
- safe mode bez rejestracji PCI.

## Ostatni wynik runtime

QEMU q35/UEFI, profil debug, base GPT + osobny tagged scratch:

- kernel self-tests: **PASS**;
- Device Manager: **7 devices**;
- Driver Manager: **1 driver (`ahci`)**;
- AHCI: **1 controller, 2 block devices**;
- GPT: **2 partitions** na base IMG;
- write/flush/readback/restore: **PASS**;
- FAT32/VFS read `/etc/system.conf` (51 bytes): **PASS**;
- shell integration: **PASS**.

Safe mode na tym samym obrazie: PCI = 0, network = skipped, shell integration = **PASS**.

Pełny `scripts/verify.ps1` po wszystkich zmianach: **PASS** (`verify-20260814-034239-f3dbda09`). Obejmuje clean debug/test/release, host tests, walidację FAT/GPT, normal/safe QEMU, tagged AHCI scratch, Foundation FAT32/VFS read oraz release ISO ShellTest. VirtualBox nie był żądany.

## Nieukończone

FAT32 write, persistence, block cache, przełączenie shella na trwały namespace, `/dev`, `/proc`, procesy, ring 3, syscalle, userspace, USB, fizyczna sieć i recovery. Szczegóły: [FOUNDATION_PROGRESS.md](FOUNDATION_PROGRESS.md) i [KNOWN_ISSUES.md](KNOWN_ISSUES.md).
