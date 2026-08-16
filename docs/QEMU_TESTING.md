# Testowanie w QEMU

## Przygotowanie

Najpierw zbuduj aktualny obraz zgodnie z [BUILDING.md](BUILDING.md). Skrypt używa repozytoryjnego `tools/qemu/qemu-system-x86_64.exe` oraz firmware EDK2 z `tools/qemu/share`.

Najprościej uruchomić z WSL2:

```bash
./scripts/run-qemu.sh smoke
./scripts/run-qemu.sh system
./scripts/run-qemu.sh iso
./scripts/run-qemu.sh safe
./scripts/run-qemu.sh img
./scripts/run-qemu.sh headless --shell-test
./scripts/run-qemu.sh debug --headless
```

## Tryby wrappera

| Tryb | Nośnik | Co jest sprawdzane |
| --- | --- | --- |
| `smoke` | katalog staged `iso/` jako FAT | osiągnięcie promptu; bez automatycznego wpisywania komend |
| `system` | `kurogane.img` w trybie snapshot | rozruch, klawiatura i scenariusz shell/RAMFS/aplikacje/sieć loopback |
| `iso` | `kurogane.iso` jako CD-ROM | osiągnięcie promptu z nośnika ISO |
| `safe` | `kurogane.img` w trybie snapshot | wybór safe mode klawiszem `S`, pominięcie sieci i ograniczony scenariusz shella |
| `img` | jawnie wybrany IMG; domyślnie snapshot | widoczne, interaktywne uruchomienie przez `run-qemu-img.ps1` |
| `headless` | jawnie wybrany IMG; domyślnie snapshot | uruchomienie bez okna przez `run-qemu-headless.ps1` |
| `debug` | jawnie wybrany IMG; domyślnie snapshot | start z zatrzymanym CPU i serwerem GDB przez `run-qemu-debug.ps1` |

`system`, `iso` i `safe` używają timeoutu 30 sekund. `smoke` dziedziczy domyślne 12 sekund z `run-qemu.ps1`.

## Bezpośrednie użycie PowerShell

Przykłady:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1 -UseDiskImage -ShellTest -TimeoutSeconds 30 -LogName local-system
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1 -UseDiskImage -DiskImagePath .\kurogane.img -Headless -TimeoutSeconds 30 -LogName explicit-system
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1 -UseDiskImage -SafeMode -ShellTest -TimeoutSeconds 30 -LogName local-safe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1 -UseIso -Display -TimeoutSeconds 30 -LogName local-iso
```

Ważniejsze opcje:

- `-UseDiskImage` uruchamia domyślny `kurogane.img` jako surowy dysk ze snapshotem;
- `-DiskImagePath <plik>` wybiera konkretny istniejący obraz raw i jednocześnie włącza tryb disk image;
- bez `-WritableDiskImage` systemowy obraz jest zawsze chroniony przez `snapshot=on`;
- `-WritableDiskImage` wymaga jawnego `-DiskImagePath` i podpina dokładnie ten plik z `snapshot=off`;
- `-WritableScratchDiskPath <plik>` w trybie disk-image podpina drugi, istniejący i oddzielny dysk SATA na porcie 1, zawsze zapisywalny i bez snapshotu;
- `-UseIso` uruchamia `kurogane.iso`; nie można łączyć obu przełączników;
- bez obu opcji QEMU używa staged `iso/` jako FAT;
- `-ShellTest` wstrzykuje znaki przez monitor QEMU i weryfikuje oczekiwany serial;
- `-SafeMode` wysyła `S`, gdy serial pokaże banner loadera;
- QEMU domyślnie otwiera okno; `-Headless` dodaje `-display none`. Parametr `-Display` zachowano chwilowo dla zgodności wrapperów, ale nie zmienia obecnego zachowania;
- `-KeepRunning` pozostawia proces do ręcznego badania i wypisuje PID;
- `-MemoryMiB` przyjmuje 64–4096 MiB, domyślnie 256 MiB;
- `-MonitorPort` i `-LogName` pozwalają uniknąć kolizji równoległych uruchomień.

## Dedykowane wrappery IMG

Widoczne uruchomienie wybranego obrazu, pozostawione aktywne po osiągnięciu promptu:

```powershell
.\scripts\run-qemu-img.ps1
```

Ograniczony czasowo test bez okna:

```powershell
.\scripts\run-qemu-headless.ps1 -ShellTest
```

Debug uruchamia CPU w stanie zatrzymanym (`-S`), pozostawia QEMU aktywne i otwiera GDB na porcie 1234:

```powershell
.\scripts\run-qemu-debug.ps1 -ImagePath .\kurogane.img -Headless
.\tools\compiler\x86_64-elf\bin\x86_64-elf-gdb.exe .\build\kernel.elf -ex "target remote 127.0.0.1:1234"
```

Wrappery Bash `run-qemu-img.sh`, `run-qemu-headless.sh` i `run-qemu-debug.sh` przyjmują ścieżki WSL i konwertują je przez `wslpath`. Ich pomoc jest dostępna przez `--help`.

Wrappery IMG bez jawnej ścieżki wybierają `state/KuroganeOS.img`, jeżeli istnieje, a w przeciwnym razie deterministyczny `build/images/KuroganeOS-base.img`. Zwykły build generuje base image i aktualizuje loader/kernel wyłącznie na ESP working image, zachowując jego partycję root. Pełne odtworzenie wymaga jawnego `build-foundation-image.ps1 -ResetWorkingImage`.

## Obrazy zapisywalne i scratch

Tryb zapisywalny jest celowo dwustopniowy. Trzeba podać zarówno konkretną ścieżkę, jak i przełącznik zapisu:

```powershell
.\scripts\run-qemu-img.ps1 -ImagePath .\build\test-disks\working-copy.img -Writable
```

Drugi dysk dla testów zapisu AHCI można dołączyć tak:

```powershell
.\scripts\run-qemu-headless.ps1 `
  -ImagePath .\kurogane.img `
  -ScratchDiskPath .\build\test-disks\ahci-scratch.img
```

Skrypty nie tworzą, nie kopiują i nie zerują obrazów. Każdy wskazany plik musi już istnieć, używać rozszerzenia `.img` albo `.raw`, mieć rozmiar podzielny przez 512 i — dla trybu writable — nie może mieć atrybutu read-only. System image i scratch nie mogą wskazywać tego samego pliku. Scratch nie może być również repozytoryjnym obrazem bootowym, ISO, firmware ani staged kernelem. Jeżeli scratch leży wewnątrz repozytorium, jego ścieżka musi znajdować się pod `build/test-disks/`; obrazy poza repozytorium są dozwolone po jawnym wskazaniu.

`-Writable`/`-WritableDiskImage` oznacza realną zgodę na modyfikację wybranego pliku przez guest. Do eksperymentów należy przygotować osobną kopię poza tymi wrapperami i przekazać jej dokładną ścieżkę.

Sama obecność tego interfejsu nie jest testem trwałości. AHCI read/write/flush, GPT oraz read-only `PartitionDevice → FAT32 → VFS` są testowane w QEMU, ale kernel shell nadal zapisuje do RAMFS. Dwubootowy test persistence pozostaje **NIEZAIMPLEMENTOWANY / NIEPRZECHODZĄCY**.

## Konfiguracja maszyny testowej

Skrypt uruchamia maszynę `q35`, jeden CPU `max`, firmware UEFI z osobnym snapshotem zmiennych, jeden serial do pliku, bez emulowanej karty sieciowej oraz z wyłączonym automatycznym restartem i wyłączeniem. Systemowy IMG jest podpinany jawnie jako SATA port 0 kontrolera ICH9 AHCI. Opcjonalny scratch trafia na SATA port 1. Brak NIC jest zamierzony: obecny test `net ping` dotyczy wyłącznie programowego adresu `127.0.0.1`.

## Scenariusz `system`

Test sprawdza między innymi:

- wersję i deskryptor ABI z transportem ring 3 oznaczonym jako niedostępny;
- statystyki heapu, CWD i ścieżki względne;
- `mkdir`, `write`, `cp`, `mv`, `cat`, `stat` i `rmdir` w RAMFS;
- `whoami` zwracające `kernel` oraz rosnący uptime;
- loopback `net ping`;
- dane Device/Driver Managera oraz `diskinfo`;
- dla Foundation base: mount `KURO_ROOT` i odczyt `/etc/system.conf` przez VFS;
- listę aplikacji i uruchomienie pełnoekranowego `desktop`, zamykane klawiszem `Q`;
- znacznik końcowy `shelltestpass`.

Scenariusz `safe` wymaga banneru safe mode, znacznika `network_loopback: SKIP`, poprawnego CWD, odczytu `/home/readme.txt`, statystyk pamięci, `whoami` i znacznika `safemodepass`.

## Wynik i logi

Logi są zapisywane jako:

```text
build/logs/<LogName>-serial.log
build/logs/<LogName>-stdout.log
build/logs/<LogName>-stderr.log
```

Test kończy się sukcesem dopiero po znalezieniu promptu i — dla `-ShellTest` — wszystkich wymaganych wzorców. `KERNEL EXCEPTION`, `KERNEL PANIC` albo `fatal:` przerywa oczekiwanie. Przy niepowodzeniu najpierw sprawdź serial; stdout QEMU zwykle jest pusty.

## Test ręczny i safe mode

Z `-Display` można obsługiwać system klawiaturą. Loader wyświetla `Press S or F8 for safe mode...` i odpytuje wejście 75 razy z opóźnieniem 10 ms, więc klawisz trzeba nacisnąć na początku rozruchu. Po starcie safe mode powinien pokazać banner diagnostyczny; nie zarejestruje widoków GUI i nie uruchomi stosu sieciowego.

Test QEMU nie dowodzi działania procesów, trwałego zapisu FAT32, fizycznej karty sieciowej, myszy ani prawdziwego sprzętu. Są to jawne ograniczenia z [CURRENT_LIMITATIONS.md](CURRENT_LIMITATIONS.md).
