# Audyt KuroganeOS Foundation

Data aktualizacji: 14 sierpnia 2026 r.

## Zasada klasyfikacji

`Working` oznacza zachowanie potwierdzone testem hostowym lub wykonaniem ścieżki w QEMU. Test hostowy parsera nie jest dowodem działania urządzenia. Kompilacja userspace API nie jest dowodem ring 3. Milestone trwałego filesystemu wymaga zapisu, `sync` i odczytu po restarcie.

GUI pozostaje w repozytorium jako `EXPERIMENTAL`; domyślny jest Console Mode.

## Podsumowanie

Projekt ma stabilną ścieżkę UEFI → kernel → konsola, własne podstawowe biblioteki, centralny model statusów, framework urządzeń i działający sprzętowy storage AHCI w referencyjnym QEMU. Najważniejsza luka to brak połączenia urządzeń blokowych i partycji z zamontowanym, zapisywalnym VFS/FAT32. Nie istnieje jeszcze prawdziwy model procesów ani userspace.

## Boot i kernel

- Własny `BOOTX64.EFI` waliduje ELF64 PIE, segmenty, zakresy i `R_X86_64_RELATIVE`, przekazuje mapę pamięci/GOP/RSDP oraz bezpiecznie wykonuje `ExitBootServices`.
- Protokół v2 obsługuje Console Mode, jawny Desktop Experimental, safe mode i diagnostykę.
- GDT ma selektory ring 0 i zarezerwowane ring 3; TSS ma osobny RSP0 i stosy IST.
- IDT, wyjątki, PIC, PIT 100 Hz, RTC i PS/2 działają w modelu jednoprocesorowym.
- Panic raportuje rejestry i ograniczony stack trace. Brak trwałego bufora logów, `dmesg`, PID/TID i pełnego raportu procesu.
- APIC/IOAPIC, HPET, SMP i ACPI poza przekazaniem RSDP nie są zaimplementowane.

## Pamięć

- PMM ma bitmapową alokację stron 4 KiB i testy błędów/reuse, ale zarządza tylko jednym wybranym regionem.
- VMM chodzi po czterech poziomach, tworzy prywatny PML4, przełącza CR3 i przechodzi runtime map/write/translate/unmap test.
- Niższe identity mappings pochodzą nadal z firmware; brak docelowego HHDM, pełnego W^X/NX, user address spaces i `copyin/copyout`.
- Heap ma `kmalloc/kcalloc/krealloc/kfree`, splitting/coalescing i statystyki, lecz stały rozmiar około 2 MiB.

## Biblioteki kernela

`kernel/libk/` oddziela funkcje freestanding od przyszłej libc. Zawiera `KStatus`, memory/string/format, bitmap/list/queue/ring buffer, hash, CRC32, UTF-8, math, atomics, logging i assert. Testy hostowe przechodzą. Starsze API podsystemów jest migrowane stopniowo przez jawne mapowanie statusów.

## Device i Driver Framework

- Device Manager przechowuje ID, klasę, bus, identyfikatory, status, resources, owner driver i relacje parent/children.
- Ownership jest wyłączny. Driver Manager wykonuje match, probe, claim, attach oraz zwalnia claim po błędzie.
- Kandydat wybierany jest priorytetem; nieudany probe pozwala użyć kolejnego drivera.
- Lifecycle dostaje obowiązkowy timeout budget. Sterowniki sprzętowe także używają bounded polling.
- PCI rejestruje odkryte urządzenia. AHCI controller jest właścicielem dwóch dzieci typu Block w teście QEMU.
- `device` i `driver` udostępniają dane frameworka w shellu.

Braki: osobny Resource Manager dla IRQ/MMIO/DMA, hotplug, detach runtime, MSI/MSI-X i synchronizacja SMP.

## Storage

- Wspólny block contract waliduje read/write/flush, geometrię i zakres LBA.
- AHCI na q35 wykonuje IDENTIFY, odczyt, zapis i flush z timeoutami.
- Tagged scratch disk ogranicza destrukcyjny test do LBA 8–15 i po weryfikacji odtwarza dane.
- GPT waliduje nagłówek, entry array, CRC, granice i overlap. W QEMU wykrywa dwie partycje base IMG przez AHCI.
- Obraz base ma protective MBR, primary/backup GPT, ESP FAT32 i root FAT32. Working image zachowuje root przy rebuildzie.

Braki do Milestone 3: publikacja partycji jako pełnych block devices używanych przez VFS, block cache, runtime mount FAT32, write support, `sync`, unmount i test persistence boot #1/#2. Parser backup GPT i MBR fallback także nie są gotowe.

## Filesystem i shell

- RAMFS zapewnia katalogi, pliki, create/read/write/remove/copy/move/stat i jest testowany także pod presją błędów.
- `PartitionDevice`, FAT32 i VFS są połączone w runtime. Na base IMG kernel montuje `KURO_ROOT` read-only i odczytuje `/etc/system.conf` przez AHCI; test QEMU wymaga markera `fat32_vfs_read: PASS`.
- Parser FAT32 akceptuje zgodne ze specyfikacją `..` z klastrem `0`, gdy rodzicem jest root; przypadek jest testowany na obrazie generowanym przez `mkfs.fat`/`mtools`.
- Kernel shell nadal używa zapisywalnego RAMFS, więc jego dane są ulotne. FAT32 nie ma jeszcze mutacji.
- Brak `/dev`, `/proc`, deskryptorów plików, uprawnień, pipe i per-process CWD/root.
- Shell działa w ring 0. Quoting, historia i operacje RAMFS działają; brak TTY, redirection, PATH i uruchamiania ELF.

## Procesy i userspace

Aktualny scheduler to dispatcher callbacków. Nie zapisuje kontekstu CPU, nie ma osobnych stosów tasków, preemption, blocked/wait ani cleanup procesu. Ring 3, syscall entry/return, FD, IPC, kernel ELF loader, PID 1, KuroLibC runtime i KuroPOSIX nie istnieją. Nagłówki SDK są compile-only i uczciwie zgłaszają brak transportu.

## Network, USB i grafika

- Ethernet/ARP/IPv4/ICMP oraz loopback są testowane jako kod protokołów; brak NIC, DHCP, UDP/TCP/DNS/sockets.
- USB/xHCI/HID/Mass Storage nie są zaimplementowane.
- Framebuffer i demonstracyjne widoki są `EXPERIMENTAL`; brak myszy, compositora i window managera.

## Build i testy

- `scripts/build.ps1` jest źródłem prawdy i obsługuje `debug/release/test`.
- `config/features.conf` generuje spójne `CONFIG_*`; manifest zapisuje profil, narzędzia, flagi, sterowniki i warstwy kompatybilności.
- Locki buildu/verify zapobiegają równoległemu zapisywaniu tych samych artefaktów. Rebuild zachowuje `build/logs`.
- Walidacja obejmuje ELF/PE, relokacje, brak RWE, FAT32, GPT, host tests, QEMU normal/safe/test-profile/AHCI oraz release ISO.

Zweryfikowany 14 sierpnia runtime: normal QEMU i safe mode **PASS**, AHCI read/write/flush **PASS**, GPT z AHCI **PASS**, Device/Driver Manager **PASS**, read-only root FAT32/VFS i `/etc/system.conf` **PASS**. Końcowy agregator `verify-20260814-034239-f3dbda09` przeszedł wszystkie wymagane etapy debug/test/release/ISO; VirtualBox był zgodnie z domyślną polityką pominięty.

## Najbliższa kolejność

1. FAT32 create/write/rename/delete nad istniejącym runtime mountem.
2. Block cache, `sync`, unmount i test dwóch bootów.
3. `/dev` i `/proc`.
4. ACPI core, APIC/IOAPIC i centralny time core.
5. xHCI → USB Core → HID.
6. Thread/process model, context switch, ring 3 i syscall ABI.
7. KuroLibC, init, Service Manager i KuroPOSIX.
8. NIC i pełny network stack.
9. Recovery/Boot Health i aktualizacje A/B.
