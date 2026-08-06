# KuroganeOS 1.0

KuroganeOS to edukacyjny, 64-bitowy system operacyjny uruchamiany w środowisku UEFI. Wersja 1.0 zawiera własny bootloader, jądro systemu, terminal, podstawowe sterowniki, system plików RAMFS, kooperacyjny planista zadań, podstawowy stos sieciowy z interfejsem loopback oraz proste aplikacje graficzne.

Oznaczenie **1.0** odnosi się do pierwszego kompletnego i uruchamialnego etapu projektu. Nie oznacza ono gotowości produkcyjnej ani zgodności ze standardami POSIX.

Referencyjnym i zweryfikowanym środowiskiem uruchomieniowym jest **QEMU z firmware EDK2** na architekturze **x86-64**.

## Ważne — brakujące pliki do budowania

Przed rozpoczęciem budowania projektu pobierz brakujące pliki używane w obecnym środowisku kompilacji:

[**Pobierz wymagane pliki z Google Drive**](https://drive.google.com/file/d/1sHfNdDOOVeJh3Q0FOtUlqPbHZIZ-ykEk/view?usp=sharing)

Po pobraniu wypakuj lub skopiuj zawartość paczki bezpośrednio do głównego katalogu repozytorium KuroganeOS.

Pliki te są wymagane przez aktualny proces budowania projektu. W przyszłych aktualizacjach sposób ich dostarczania może zostać zmieniony.

## Dokumentacja

Dodatkowe informacje o projekcie znajdują się w dokumentacji:

* [Roadmapa i status kamieni milowych](docs/roadmap-0.0.1.md)
* [Architektura systemu](docs/architektura.md)

## Aktualnie zaimplementowane funkcje

KuroganeOS 1.0 oferuje obecnie:

* samodzielną aplikację UEFI `BOOTX64.EFI`, która ładuje jądro w formacie ELF64;
* przekazywanie do jądra mapy pamięci, framebuffera GOP oraz informacji ACPI;
* poprawne zakończenie usług startowych UEFI przy użyciu `ExitBootServices`;
* terminal renderowany bezpośrednio w framebufferze;
* kopię wyjścia terminala przesyłaną na port szeregowy;
* stertę jądra;
* bitmapowy alokator ramek pamięci fizycznej;
* tablicę IDT i obsługę wyjątków procesora;
* obsługę kontrolera PIC;
* timer PIT;
* obsługę klawiatury PS/2;
* wykrywanie urządzeń PCI;
* odczyt czasu z zegara RTC;
* hierarchiczny i zapisywalny system plików RAMFS;
* kooperacyjne zadania okresowe i jednorazowe;
* podstawową obsługę Ethernet II, ARP, IPv4 i ICMP;
* pamięciowy interfejs sieciowy loopback;
* powłokę systemową;
* podstawowy pulpit graficzny;
* aplikacje `desktop`, `monitor`, `files` oraz `about`.

## Wymagania

Kanoniczny proces budowania na Windows wykorzystuje:

* PowerShell;
* cross-toolchain `x86_64-elf`;
* QEMU;
* firmware EDK2;
* pliki dostarczone w katalogu `tools`.

Cross-toolchain powinien znajdować się w katalogu:

```text
tools/compiler/x86_64-elf/bin
```

QEMU oraz firmware EDK2 powinny znajdować się w katalogu:

```text
tools/qemu
```

## Budowanie projektu

W głównym katalogu repozytorium uruchom:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Rebuild
```

Skrypt budowania wykonuje następujące operacje:

1. kompiluje wszystkie źródła jądra;
2. linkuje plik `build/kernel.elf`;
3. buduje bootloader UEFI;
4. konwertuje bootloader do formatu PE32+;
5. sprawdza formaty wygenerowanych plików;
6. wykonuje podstawową kontrolę właściwości bezpieczeństwa obrazów;
7. przygotowuje strukturę startową w katalogu `iso`;
8. tworzy deterministyczny obraz FAT32 o rozmiarze 64 MiB;
9. zapisuje gotowy obraz jako `kurogane.img`.

## Wygenerowane pliki

Po zakończeniu procesu budowania najważniejsze pliki powinny znajdować się w następujących lokalizacjach:

```text
build/kernel.elf
build/BOOTX64.EFI
iso/kernel.elf
iso/EFI/BOOT/BOOTX64.EFI
kurogane.img
```

## Opcje skryptu budowania

Skrypt `build.ps1` obsługuje dodatkowe przełączniki:

* `-Clean` — usuwa wszystkie wygenerowane pliki;
* `-Rebuild` — wykonuje pełne przebudowanie projektu;
* `-NoStage` — buduje jądro bez przygotowywania katalogu `iso`;
* `-StageOnly` — ponownie buduje bootloader, przygotowuje katalog `iso` i tworzy obraz FAT32, wykorzystując istniejący plik `build/kernel.elf`.

## Uruchamianie w QEMU

Do uruchamiania KuroganeOS służy skrypt:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1
```

Domyślnie skrypt wykonuje automatyczny test startowy bez otwierania okna QEMU.

Maszyna testowa korzysta z:

* chipsetu `q35`;
* 256 MiB pamięci RAM;
* firmware UEFI EDK2;
* konsoli szeregowej zapisywanej do pliku;
* automatycznego wykrywania promptu powłoki.

Log konsoli szeregowej zostaje zapisany w pliku:

```text
build/qemu-serial.log
```

Skrypt oczekuje na pojawienie się promptu:

```text
kurogane:/ $
```

Po jego wykryciu test zostaje uznany za zakończony powodzeniem, a maszyna wirtualna jest zatrzymywana.

## Zmiana limitu czasu testu

Domyślny limit czasu można zmienić za pomocą parametru `-TimeoutSeconds`.

Przykład:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1 -TimeoutSeconds 30
```

## Rozszerzony test systemu

Pełny test klawiatury, powłoki, RAMFS, sieci i interfejsu graficznego można uruchomić poleceniem:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1 -ShellTest -UseDiskImage -TimeoutSeconds 30
```

Test uruchamia system bezpośrednio z obrazu FAT32 i automatycznie wykonuje wybrane polecenia powłoki.

## Tryb interaktywny

Aby uruchomić KuroganeOS z aktywnym ekranem i obsługą klawiatury, użyj:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-qemu.ps1 -Display -KeepRunning
```

Opcja `-Display` otwiera okno maszyny wirtualnej, natomiast `-KeepRunning` zapobiega jej automatycznemu zamknięciu po zakończeniu testu startowego.

## Polecenia powłoki

| Polecenie                           | Działanie                                                |
| ----------------------------------- | -------------------------------------------------------- |
| `help`                              | Wyświetla listę dostępnych poleceń.                      |
| `clear`                             | Czyści zawartość terminala.                              |
| `version`                           | Wyświetla wersję systemu.                                |
| `uname`                             | Wyświetla wersję systemu i architekturę.                 |
| `echo <tekst>`                      | Wyświetla podany tekst.                                  |
| `date`                              | Wyświetla czas odczytany z zegara RTC.                   |
| `uptime`                            | Wyświetla czas działania jądra.                          |
| `mem`                               | Wyświetla stan sterty i ramek pamięci fizycznej.         |
| `pci`                               | Wyświetla wykryte urządzenia i funkcje PCI.              |
| `net`                               | Wyświetla konfigurację i statystyki interfejsu loopback. |
| `net ping`                          | Wykonuje test ICMP do adresu `127.0.0.1`.                |
| `tasks`                             | Wyświetla zadania zarejestrowane w planiście.            |
| `apps`                              | Wyświetla listę dostępnych aplikacji.                    |
| `run <aplikacja>`                   | Uruchamia wskazaną aplikację.                            |
| `gui`                               | Uruchamia graficzny pulpit systemu.                      |
| `ls [ścieżka]`                      | Wyświetla zawartość katalogu RAMFS.                      |
| `cat <ścieżka>`                     | Wyświetla zawartość pliku RAMFS.                         |
| `touch <ścieżka>`                   | Tworzy nowy plik w RAMFS.                                |
| `mkdir <ścieżka>`                   | Tworzy nowy katalog w RAMFS.                             |
| `write <ścieżka> <tekst>`           | Zapisuje tekst we wskazanym pliku RAMFS.                 |
| `rm <ścieżka>`                      | Usuwa plik z RAMFS.                                      |
| `rm -r <ścieżka>`                   | Rekurencyjnie usuwa katalog i jego zawartość.            |
| `calc <liczba> <operator> <liczba>` | Wykonuje podstawowe działanie matematyczne.              |
| `reboot`                            | Próbuje ponownie uruchomić maszynę.                      |
| `poweroff`                          | Próbuje wyłączyć maszynę.                                |

Polecenie `calc` obsługuje operatory:

```text
+  -  *  /  %
```

## Aplikacje graficzne

Dostępne są następujące aplikacje:

| Aplikacja | Opis                                        |
| --------- | ------------------------------------------- |
| `desktop` | Uruchamia główny pulpit graficzny.          |
| `monitor` | Wyświetla podstawowe informacje o systemie. |
| `files`   | Otwiera prostą przeglądarkę plików RAMFS.   |
| `about`   | Wyświetla informacje o KuroganeOS.          |

Aplikację można uruchomić poleceniem:

```text
run <nazwa-aplikacji>
```

Przykład:

```text
run monitor
```

Graficzny pulpit można również uruchomić skróconym poleceniem:

```text
gui
```

## Sterowanie interfejsem graficznym

W aplikacjach graficznych:

* `Q` — zamyka aplikację i wraca do powłoki;
* `Esc` — zamyka aplikację i wraca do powłoki.

Na pulpicie dostępne są również skróty:

* `M` — otwiera monitor systemu;
* `F` — otwiera przeglądarkę plików;
* `A` — otwiera informacje o systemie.

## Zweryfikowany stan wydania

Dnia **26 lipca 2026 roku** kompletny zestaw startowy KuroganeOS został uruchomiony w środowisku:

* QEMU 11.0.0;
* firmware EDK2;
* architektura x86-64;
* maszyna `q35`.

Automatyczny test startowy osiągnął interaktywny prompt powłoki.

Konsola szeregowa potwierdziła:

* poprawne wejście do jądra po wykonaniu `ExitBootServices`;
* zakończenie testu pamięci komunikatem `memory self-test: PASS`;
* gotowość przerwań, timera i klawiatury komunikatem `interrupts/timer/keyboard: READY`;
* poprawne skonfigurowanie kontrolera PS/2;
* zakończenie skanowania magistrali PCI;
* poprawne działanie interfejsu loopback potwierdzone komunikatem `network loopback: PASS (127.0.0.1)`;
* wyświetlenie promptu `kurogane:/ $`.

Rozszerzony test wykonywany przez emulowaną klawiaturę potwierdził również działanie poleceń:

```text
version
mem
cat /system/version
net ping
apps
gui
```

Test potwierdził także możliwość uruchomienia i zamknięcia pulpitu graficznego.

## Testy

Źródła testów modułowych znajdują się w katalogu:

```text
tests
```

Obecnie dostępne są testy dotyczące:

* alokatora pamięci;
* systemu plików RAMFS;
* planisty zadań;
* stosu sieciowego.

Aktualny skrypt `build.ps1` nie uruchamia testów modułowych automatycznie. Test startu w QEMU należy wykonać jako osobny krok.

## Eksperymentalne SDK

Publiczne nagłówki ABI zostały oddzielone od prywatnych nagłówków jądra i znajdują się w katalogu:

```text
sdk/include
```

Skrypt:

```bash
./scripts/build-sdk.sh
```

wykonuje następujące operacje:

1. generuje sysroot SDK;
2. zapisuje go w katalogu `build/sdk/sysroot`;
3. kompiluje zewnętrzny przykład aplikacji w trybie freestanding.

Polecenie powłoki:

```text
abi
```

wyświetla wersję deskryptora ABI oraz informacje o dostępnych funkcjach.

SDK stanowi obecnie jedynie fundament przyszłego ABI. Nie jest jeszcze kompletnym środowiskiem uruchomieniowym aplikacji.

Nadal niezaimplementowane pozostają:

* transport wywołań systemowych;
* procesy działające w ring 3;
* pliki startowe aplikacji;
* ładowanie wykonywalnych programów;
* stabilne linkowanie aplikacji użytkownika;
* pełny runtime użytkownika.

Z tego powodu deskryptor ABI zgłasza obecnie bitmapę dostępnych funkcji równą zero.

## Ograniczenia

KuroganeOS 1.0 pozostaje projektem edukacyjnym i demonstracyjnym.

### System plików

RAMFS jest systemem ulotnym. Cała jego zawartość zostaje utracona po ponownym uruchomieniu systemu.

Aktualne limity wynoszą:

* maksymalnie 256 węzłów;
* maksymalnie 64 KiB danych na pojedynczy plik;
* maksymalnie 1 MiB danych wszystkich plików łącznie.

System nie posiada jeszcze:

* sterownika trwałego nośnika danych;
* obsługi montowania partycji FAT;
* obsługi AHCI;
* obsługi NVMe.

### Zadania i procesy

Planista jest kooperacyjny i wykonuje callbacki bezpośrednio w kontekście jądra.

Nie zaimplementowano jeszcze:

* przełączania stosów;
* planowania z wywłaszczaniem;
* procesów użytkownika;
* separacji procesów;
* izolacji pamięci;
* poziomów uprawnień aplikacji.

### Sieć

Stos Ethernet, ARP, IPv4 i ICMP działa obecnie wyłącznie przez pamięciowy interfejs loopback.

Nie zaimplementowano jeszcze:

* sterownika fizycznej karty sieciowej;
* DHCP;
* TCP;
* UDP;
* DNS;
* dostępu do Internetu.

### Interfejs graficzny

GUI wykorzystuje podstawowe rysowanie bezpośrednio w framebufferze.

Nie zaimplementowano jeszcze:

* obsługi myszy;
* pełnego menedżera okien;
* kompozytora;
* akceleracji graficznej;
* wielozadaniowych aplikacji użytkownika;
* zaawansowanego systemu zdarzeń.

### Sterowniki

Aktualny zestaw sterowników ogranicza się do urządzeń wymaganych przez środowisko demonstracyjne.

Nie zaimplementowano między innymi:

* USB;
* audio;
* obsługi wielu procesorów;
* nowoczesnych kontrolerów pamięci masowej;
* fizycznych kart sieciowych;
* zaawansowanego zarządzania energią.

### Zgodność sprzętowa

Niezawodność KuroganeOS została zweryfikowana wyłącznie w referencyjnej konfiguracji QEMU i EDK2.

Uruchamianie systemu na rzeczywistym sprzęcie UEFI nie jest jeszcze częścią oficjalnie przetestowanego zakresu projektu.

## Dalszy rozwój

KuroganeOS stanowi punkt wyjścia do dalszej pracy nad:

* pamięcią wirtualną;
* stronicowaniem;
* trybem użytkownika;
* procesami ring 3;
* wywołaniami systemowymi;
* trwałym systemem plików;
* sterownikami dysków;
* obsługą USB;
* obsługą myszy;
* sterownikami sieciowymi;
* protokołami TCP, UDP i DNS;
* planowaniem z wywłaszczaniem;
* izolacją aplikacji;
* stabilnym ABI;
* pełnym menedżerem okien;
* bardziej rozbudowanym środowiskiem graficznym.

## Status projektu

KuroganeOS jest projektem eksperymentalnym i edukacyjnym. Nie powinien być obecnie używany jako system produkcyjny ani jako środowisko przechowujące ważne dane.
