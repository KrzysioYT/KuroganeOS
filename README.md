# KuroganeOS 1.0

KuroganeOS jest edukacyjnym, 64-bitowym systemem operacyjnym uruchamianym
przez UEFI. Wydanie 1.0 łączy własny bootloader, kernel, terminal, podstawowe
sterowniki, RAMFS, kooperacyjny planista, stos sieciowy z interfejsem loopback
oraz proste aplikacje graficzne.

Określenie „1.0” oznacza tutaj pierwszy kompletny i uruchamialny zakres
projektu, a nie gotowość produkcyjną ani zgodność z systemami POSIX.
Zweryfikowaną platformą referencyjną jest QEMU/EDK2 na architekturze x86-64.

Szczegóły:

- [roadmap i status kamieni milowych](docs/roadmap-0.0.1.md),
- [architektura systemu](docs/architektura.md).

## Co działa

- samodzielna aplikacja UEFI `BOOTX64.EFI`, która ładuje kernel ELF64;
- przekazanie mapy pamięci, framebuffera GOP i danych ACPI do kernela;
- zakończenie usług startowych UEFI przez `ExitBootServices`;
- terminal na framebufferze z kopią wyjścia na port szeregowy;
- sterta kernela i bitmapowy alokator ramek pamięci fizycznej;
- IDT, obsługa wyjątków, PIC, timer PIT i klawiatura PS/2;
- wykrywanie urządzeń PCI oraz odczyt zegara RTC;
- hierarchiczny, zapisywalny RAMFS;
- kooperacyjne zadania okresowe i jednorazowe;
- Ethernet II, ARP, IPv4 i ICMP na interfejsie loopback;
- shell oraz aplikacje `desktop`, `monitor`, `files` i `about`.

## Budowanie

Kanoniczny proces budowania na Windows używa PowerShella i dołączonego do
repozytorium cross-toolchaina `x86_64-elf` z katalogu
`tools/compiler/x86_64-elf/bin`.

W katalogu głównym repozytorium uruchom:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Rebuild
```

Skrypt:

1. kompiluje wszystkie źródła kernela;
2. linkuje `build/kernel.elf`;
3. buduje bootloader UEFI i konwertuje go do PE32+;
4. sprawdza formaty i podstawowe właściwości bezpieczeństwa obrazów;
5. umieszcza pliki startowe w katalogu `iso`;
6. tworzy deterministyczny, 64 MiB obraz FAT32 `kurogane.img`.

Najważniejsze wyniki:

```text
build/kernel.elf
build/BOOTX64.EFI
iso/kernel.elf
iso/EFI/BOOT/BOOTX64.EFI
kurogane.img
```

Dodatkowe przełączniki skryptu:

- `-Clean` — usuwa wyniki budowania;
- `-NoStage` — buduje kernel bez przygotowania katalogu `iso`;
- `-StageOnly` — ponownie buduje bootloader, przygotowuje `iso` oraz obraz
  FAT32, korzystając z istniejącego `build/kernel.elf`.

## Uruchamianie w QEMU

Skrypt oczekuje QEMU i firmware EDK2 w katalogu `tools/qemu`. Automatyczny,
bezokienkowy test startu wykonuje polecenie:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1
```

Test uruchamia maszynę `q35` z 256 MiB RAM, zapisuje konsolę szeregową do
`build/qemu-serial.log`, czeka na prompt `kurogane:/ $`, a następnie zatrzymuje
maszynę. Limit czasu można zmienić, na przykład:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1 -TimeoutSeconds 30
```

Pełny test klawiatury, shella, RAMFS, sieci i uruchomienia GUI bezpośrednio
z obrazu FAT32:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1 -ShellTest -UseDiskImage -TimeoutSeconds 30
```

Do pracy interaktywnej z ekranem i klawiaturą:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1 -Display -KeepRunning
```

## Polecenia shella

| Polecenie | Działanie |
| --- | --- |
| `help` | Wyświetla listę poleceń. |
| `clear` | Czyści terminal. |
| `version`, `uname` | Pokazuje wersję i architekturę. |
| `echo <tekst>` | Wypisuje argumenty. |
| `date`, `uptime` | Pokazuje czas RTC lub czas działania kernela. |
| `mem` | Pokazuje stan sterty i ramek fizycznych. |
| `pci` | Wyświetla wykryte funkcje PCI. |
| `net`, `net ping` | Pokazuje konfigurację/statystyki loopback lub wykonuje ping do `127.0.0.1`. |
| `tasks` | Wyświetla zadania planisty. |
| `apps` | Wyświetla zarejestrowane aplikacje. |
| `run <aplikacja>` | Uruchamia aplikację `desktop`, `monitor`, `files` albo `about`. |
| `gui` | Uruchamia graficzny pulpit. |
| `ls [ścieżka]`, `cat <ścieżka>` | Wyświetla katalog lub zawartość pliku RAMFS. |
| `touch <ścieżka>`, `mkdir <ścieżka>` | Tworzy plik lub katalog RAMFS. |
| `write <ścieżka> <tekst>` | Zapisuje tekst w pliku RAMFS. |
| `rm [-r] <ścieżka>` | Usuwa plik albo drzewo katalogów RAMFS. |
| `calc <liczba> <operator> <liczba>` | Wykonuje działanie `+`, `-`, `*`, `/` lub `%`. |
| `reboot`, `poweroff` | Próbuje zrestartować lub wyłączyć maszynę. |

W aplikacjach graficznych klawisz `Q` lub `Esc` wraca do shella. Na pulpicie
klawisze `M`, `F` i `A` otwierają odpowiednio monitor, przeglądarkę plików
i informacje o systemie.

## Zweryfikowany stan wydania

26 lipca 2026 r. kompletny zestaw startowy został uruchomiony w QEMU 11.0.0
z firmware EDK2. Automatyczny test osiągnął interaktywny prompt. Konsola
szeregowa potwierdziła:

- wejście do kernela po `ExitBootServices`;
- `memory self-test: PASS`;
- `interrupts/timer/keyboard: READY`;
- skonfigurowany kontroler PS/2;
- zakończone skanowanie PCI;
- `network loopback: PASS (127.0.0.1)`;
- prompt `kurogane:/ $`.

Rozszerzony test przez emulowaną klawiaturę potwierdził również polecenia
`version`, `mem`, `cat /system/version`, `net ping`, `apps` oraz uruchomienie
i zamknięcie pulpitu przez `gui`.

Źródła testów modułowych alokatora pamięci, RAMFS, planisty i stosu sieciowego
znajdują się w katalogu `tests`. Obecny `build.ps1` nie uruchamia ich
automatycznie; test startu QEMU jest osobnym krokiem.

## Eksperymentalne SDK

Publiczne nagłówki ABI są oddzielone od prywatnych nagłówków kernela w
`sdk/include`. Polecenie `./scripts/build-sdk.sh` generuje sysroot w
`build/sdk/sysroot` i kompiluje zewnętrzny przykład w trybie freestanding.
Komenda shella `abi` pokazuje wersję deskryptora i dostępne funkcje.

Jest to fundament ABI, nie gotowy runtime aplikacji. Transport syscalli,
procesy ring 3, pliki startowe i linkowanie wykonywalnych aplikacji pozostają
niezaimplementowane, dlatego deskryptor zgłasza bitmapę funkcji równą zero.

## Ograniczenia

KuroganeOS 1.0 pozostaje systemem demonstracyjnym:

- RAMFS jest ulotny: cała zawartość znika po restarcie. Limit wynosi 256
  węzłów, 64 KiB na plik i 1 MiB danych plików łącznie;
- nie ma sterownika trwałego nośnika ani montowania FAT, AHCI lub NVMe;
- planista jest kooperacyjny i wykonuje callbacki w kontekście kernela; nie
  implementuje przełączania stosów, preempcji ani procesów użytkownika;
- aplikacje działają w jednej przestrzeni adresowej kernela, bez izolacji,
  uprawnień i stabilnego ABI użytkownika;
- stos Ethernet/ARP/IPv4/ICMP działa wyłącznie przez pamięciowy loopback.
  Brakuje sterownika fizycznej karty sieciowej, DHCP, TCP, UDP, DNS i dostępu
  do Internetu;
- GUI to podstawowe rysowanie w framebufferze i kilka aplikacji kernela.
  Nie ma myszy, menedżera okien, kompozytora ani akceleracji graficznej;
- zestaw sterowników jest ograniczony do sprzętu potrzebnego dla obecnej
  demonstracji. Nie ma między innymi USB, audio ani obsługi wielu procesorów;
- niezawodność została sprawdzona w jednej konfiguracji QEMU/EDK2. Start na
  rzeczywistym sprzęcie UEFI nie jest jeszcze częścią zweryfikowanego zakresu.

Projekt jest dobrym punktem wyjścia do dalszej pracy nad pamięcią wirtualną,
trybem użytkownika, trwałym systemem plików, sterownikami urządzeń,
preempcją i pełną komunikacją sieciową.
