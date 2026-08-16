# Build status

Data: 16 sierpnia 2026 r.

## Current stage

KuroganeOS 2.1 jest etapem **Installable System Release**. Aktualny kod obejmuje UEFI boot, procesy Ring 3, PID 1, SATA/AHCI, GPT, writable FAT32/VFS oraz instalator, który potrafi przygotować pusty wirtualny dysk i uruchomić z niego zainstalowany system.

## Working in current source

- profile `debug`, `release` i `test` oraz kanoniczny frontend `scripts/build.ps1`;
- własny UEFI `BOOTX64.EFI` i boot protocol v3 z installer payload;
- czteropoziomowe page tables, własny VMM, GDT/TSS/IST i IDT;
- Ring 3, `int 0x80`, prywatne przestrzenie adresowe i ELF64 userspace;
- process spawn/wait/exit, osobne stosy oraz timer preemption;
- `/system/init` jako PID 1 i userspace console;
- PCI, ACPI MADT, APIC discovery oraz fallback PIC;
- PS/2 keyboard, PS/2 mouse i wspólna kolejka input;
- SATA/AHCI read/write/flush;
- GPT read/write oraz protective MBR;
- writable FAT32 i persistent root przez VFS;
- installer package z bootloaderem, kernelem i userspace rootfs;
- formatowanie ESP/root, kopiowanie systemu, verification i flush;
- first-boot marker oraz test persistence między restartami;
- helper QEMU i helper VirtualBox EFI/AHCI.

## Committed QEMU evidence

Repozytorium zawiera logi z przebiegu implementacji, które dokumentują działający scenariusz instalacyjny przed finalnym bumpem wersji do 2.1:

### Installer medium / pierwszy boot

`build/logs/installer-first-boot-serial.log` pokazuje między innymi:

- inicjalizację kernela i Ring 3;
- wykrycie kontrolera AHCI;
- uruchomienie userspace z boot/installer payload na pustym dysku;
- wejście `/system/init` do Ring 3 jako PID 1;
- start userspace shell;
- `[TEST] userspace_init_spawn: PASS`.

### Deployment

`build/logs/installer-deploy-serial.log` pokazuje:

- wykrycie docelowego dysku SATA;
- utworzenie protective MBR, primary GPT i backup GPT;
- formatowanie ESP oraz persistent root FAT32;
- instalację `EFI/BOOT/BOOTX64.EFI`, kernela i `/system/init`;
- weryfikację instalacji przed zakończeniem procesu.

### Boot z zainstalowanego HDD

`build/logs/installer-second-boot-serial.log` pokazuje:

- ponowne wykrycie AHCI/GPT;
- mount persistent FAT32 root;
- odczyt zainstalowanego `/system/init`;
- start PID 1 w Ring 3;
- userspace console po uruchomieniu z dysku.

## Zmiany finalizujące 2.1

Bieżący release patch:

- ustawia centralną wersję na `2.1`;
- poprawia kolejność required-testów tak, aby globalny `ALL_REQUIRED_TESTS_PASSED` nie pojawiał się przed testem PID 1;
- generuje kanoniczny `dist/KuroganeOS-2.1-x86_64.iso`;
- generuje `dist/SHA256SUMS.txt`;
- pozostawia `kurogane.iso` wyłącznie jako lokalny compatibility artifact dla istniejących runnerów;
- aktualizuje dokumentację ze starego stanu 1.0/early-2.0 do rzeczywistej implementacji.

## Validation status for this release patch

Kod oraz dotychczasowe logi QEMU zostały przeaudytowane względem aktualnej implementacji. Nowy bump 2.1 i nowe `dist/` outputy wymagają ponownego uruchomienia build/test suite na środowisku Windows + WSL/QEMU, ponieważ środowisko wykonujące tę zmianę w repozytorium nie posiada lokalnego toolchaina KuroganeOS ani VirtualBox.

Dlatego nie należy traktować samego commita jako dowodu świeżego runtime PASS. Po lokalnym pullu zalecane jest uruchomienie pełnego `scripts/verify.ps1` oraz builda installera release.

## Known limitations / not release-complete subsystems

- pełny recovery environment (filesystem repair, rollback itd.) nadal nie jest zaimplementowany; dostępne są safe mode i diagnostics;
- VirtualBox ma helper EFI/AHCI, ale pełny interaktywny install → remove ISO → boot HDD wymaga aktualnej weryfikacji w środowisku z `VBoxManage`;
- real-hardware UEFI pozostaje słabiej zweryfikowany niż QEMU;
- NVMe, audio i szersza obsługa współczesnego sprzętu nie są kompletne;
- desktop oraz publiczne ABI/SDK pozostają eksperymentalne.
