# Testowanie w QEMU

## Przygotowanie

Najpierw zbuduj aktualny obraz zgodnie z [BUILDING.md](BUILDING.md). Skrypt używa repozytoryjnego `tools/qemu/qemu-system-x86_64.exe` oraz firmware EDK2 z `tools/qemu/share`.

Najprościej uruchomić z WSL2:

```bash
./scripts/run-qemu.sh smoke
./scripts/run-qemu.sh system
./scripts/run-qemu.sh iso
./scripts/run-qemu.sh safe
```

## Tryby wrappera

| Tryb | Nośnik | Co jest sprawdzane |
| --- | --- | --- |
| `smoke` | katalog staged `iso/` jako FAT | osiągnięcie promptu; bez automatycznego wpisywania komend |
| `system` | `kurogane.img` w trybie snapshot | rozruch, klawiatura i scenariusz shell/RAMFS/aplikacje/sieć loopback |
| `iso` | `kurogane.iso` jako CD-ROM | osiągnięcie promptu z nośnika ISO |
| `safe` | `kurogane.img` w trybie snapshot | wybór safe mode klawiszem `S`, pominięcie sieci i ograniczony scenariusz shella |

`system`, `iso` i `safe` używają timeoutu 30 sekund. `smoke` dziedziczy domyślne 12 sekund z `run-qemu.ps1`.

## Bezpośrednie użycie PowerShell

Przykłady:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1 -UseDiskImage -ShellTest -TimeoutSeconds 30 -LogName local-system
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1 -UseDiskImage -SafeMode -ShellTest -TimeoutSeconds 30 -LogName local-safe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1 -UseIso -Display -TimeoutSeconds 30 -LogName local-iso
```

Ważniejsze opcje:

- `-UseDiskImage` uruchamia `kurogane.img` jako surowy dysk ze snapshotem;
- `-UseIso` uruchamia `kurogane.iso`; nie można łączyć obu przełączników;
- bez obu opcji QEMU używa staged `iso/` jako FAT;
- `-ShellTest` wstrzykuje znaki przez monitor QEMU i weryfikuje oczekiwany serial;
- `-SafeMode` wysyła `S`, gdy serial pokaże banner loadera;
- `-Display` pokazuje okno, domyślnie używane jest `-display none`;
- `-KeepRunning` pozostawia proces do ręcznego badania i wypisuje PID;
- `-MemoryMiB` przyjmuje 64–4096 MiB, domyślnie 256 MiB;
- `-MonitorPort` i `-LogName` pozwalają uniknąć kolizji równoległych uruchomień.

## Konfiguracja maszyny testowej

Skrypt uruchamia maszynę `q35`, CPU `max`, firmware UEFI z osobnym snapshotem zmiennych, jeden serial do pliku, bez emulowanej karty sieciowej oraz z wyłączonym automatycznym restartem i wyłączeniem. Brak NIC jest zamierzony: obecny test `net ping` dotyczy wyłącznie programowego adresu `127.0.0.1`.

## Scenariusz `system`

Test sprawdza między innymi:

- wersję i deskryptor ABI z transportem ring 3 oznaczonym jako niedostępny;
- statystyki heapu, CWD i ścieżki względne;
- `mkdir`, `write`, `cp`, `mv`, `cat`, `stat` i `rmdir` w RAMFS;
- `whoami` zwracające `kernel` oraz rosnący uptime;
- loopback `net ping`;
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

Test QEMU nie dowodzi działania procesów, trwałego zapisu, fizycznej karty sieciowej, myszy ani prawdziwego sprzętu. Są to jawne ograniczenia z [CURRENT_LIMITATIONS.md](CURRENT_LIMITATIONS.md).
