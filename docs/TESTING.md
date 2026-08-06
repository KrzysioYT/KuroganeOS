# Testowanie i regresja

## Pełna macierz

Głównym agregatorem jest PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify.ps1
```

Domyślny przebieg zatrzymuje się przy pierwszym błędzie i wykonuje:

1. preflight WSL2 oraz narzędzi `bash`, `fsck.fat`, `g++`, `make`, Python 3, PowerShell, `wslpath` i `xorriso`;
2. czysty build `debug`;
3. wszystkie testy hostowe;
4. read-only `fsck.fat -vn` obrazu;
5. QEMU ShellTest z `kurogane.img`;
6. QEMU ShellTest safe mode;
7. czysty build `release`;
8. budowę release ISO;
9. QEMU ShellTest z ISO.

VirtualBox jest domyślnie pomijany, ponieważ jest zewnętrzną i wolniejszą zależnością. Pełna macierz z nim:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify.ps1 -RunVirtualBox
```

`-TimeoutSeconds` ustala timeout emulatorów w zakresie 5–120. `-KeepLogs` nadaje przebiegowi unikalną nazwę zamiast zestawu `verify-latest`; włącza też zachowanie diagnostyki VirtualBox w razie błędu. `-SkipVirtualBox` pozostaje przełącznikiem dla wspólnych poleceń automatyzacji, które chcą jawnie wyłączyć ten etap; nie można łączyć go z `-RunVirtualBox`.

## Testy hostowe

Z WSL2:

```bash
./scripts/test.sh
```

Runner kompiluje testy przez `${CXX:-g++}` z C++17, `-O2 -Wall -Wextra -Wpedantic`, uruchamia je kolejno i zapisuje wspólny log w `build/logs/host-tests.log`.

| Test | Zakres |
| --- | --- |
| `memory` | heap, wyrównania, odzyskiwanie bloków, walidacja i bitmapa PMM |
| `virtual-memory` | inicjalizacja, walidacja, map/unmap/reuse, flagi, granice, konflikty, tablice zewnętrzne i rollback OOM/backendu |
| `ramfs` | hierarchia, limity, błędy, kopia, move/rename, cykle, niezależność danych, shrink i atomowość przy OOM |
| `scheduler` | cykl życia callbacków, tick/dispatch, budżet, suspend/resume/cancel/yield i metryki |
| `network` | Ethernet/ARP/IPv4/ICMP, checksumy, błędy, sąsiedzi i loopback |
| `profiler` | snapshoty statystyk modułów i odporność na brak inicjalizacji |
| `sdk-abi` | rozmiary, wersja i walidacja publicznego deskryptora |
| `sdk-test` | makra/asercje eksperymentalnego frameworka testowego SDK |
| generator SDK | sysroot, kompilacja przykładu i zachowanie szablonów projektów |

Są to testy uruchamiane jako procesy hosta. Same nie dowodzą użycia kodu przez urządzenie ani uruchomiony kernel. Dlatego `kmain` wykonuje dodatkowo runtime self-test na aktywnych tablicach UEFI: mapuje ramkę pod wolnym adresem, zapisuje przez alias, sprawdza translację, usuwa mapowanie i kontroluje wyciek. Test QEMU zalicza ten etap tylko wtedy, gdy rozruch dociera do promptu.

## Walidacja podczas budowania

`scripts/build.ps1` odrzuca niezgodny ELF/PE, segment RWE, brak `PT_DYNAMIC`, nieobsługiwaną relokację oraz symbol niezdefiniowany. Generator FAT32 po zapisie odczytuje obraz i porównuje boot sector, FSInfo, kopie FAT, katalogi oraz zawartość staged plików. `scripts/build-iso.sh` używa tego obrazu jako UEFI El Torito.

To są testy strukturalne artefaktów. Do potwierdzenia rozruchu nadal potrzebne jest QEMU lub VirtualBox.

## QEMU

Skrócone wejścia:

```bash
./scripts/run-qemu.sh smoke
./scripts/run-qemu.sh system
./scripts/run-qemu.sh iso
./scripts/run-qemu.sh safe
```

Scenariusze, logi i warunki sukcesu opisuje [QEMU_TESTING.md](QEMU_TESTING.md). Najszerszy test QEMU w `verify.ps1` uruchamia ShellTest zarówno z dysku, safe mode, jak i z ISO; wrapper `run-qemu.sh iso` sam sprawdza tylko prompt.

## VirtualBox

Pojedynczy smoke test:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-virtualbox.ps1
```

Instrukcja i bezpieczne sprzątanie są opisane w [VIRTUALBOX_TESTING.md](VIRTUALBOX_TESTING.md).

## Logi agregatora

Domyślnie powstają:

```text
build/logs/verify-latest-status.log
build/logs/verify-latest-<etap>.log
build/logs/verify-latest-qemu-*-serial.log
```

Status zawiera `START`, `PASS`, `FAIL`, `SKIP` i czas etapu. Przy błędzie agregator pokazuje końcówkę logu i pozostawia pełny plik. `-KeepLogs` używa prefiksu `verify-<data>-<id>`.

## Co nadal wymaga testów

Nie ma testu długiego działania, realnego sprzętu, SMP, własnej kompletnej przestrzeni adresowej kernela, ring 3, syscalls, sterownika dysku/NIC/myszy, trwałości ani kompozytora. Brak takiego testu wynika przede wszystkim z braku odpowiadającej implementacji, a nie z pominięcia gotowej funkcji.

Aktualny, datowany wynik znajduje się w [BUILD_STATUS.md](BUILD_STATUS.md).
