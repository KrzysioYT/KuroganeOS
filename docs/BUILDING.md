# Budowanie KuroganeOS

## Wymagania

Podstawowy build jest przeznaczony dla Windows i korzysta z repozytoryjnego toolchainu `tools/compiler/x86_64-elf/bin`. Wymagane są Windows PowerShell oraz pliki toolchainu obecne w repozytorium. Wrappery Bash i testy hostowe wymagają WSL2 z Bash, `g++`, `make`, Pythonem 3 oraz dostępem do `powershell.exe`.

Do utworzenia ISO potrzebny jest dodatkowo `xorriso` w WSL. QEMU jest używane z `tools/qemu`; VirtualBox jest zewnętrznym wymaganiem tylko dla jego testu.

## Build kanoniczny w PowerShell

Uruchom z katalogu głównego repozytorium:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Configuration debug
```

Wersja release:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Configuration release
```

Pełna przebudowa i czyszczenie:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Configuration debug -Rebuild
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Clean
```

`-Rebuild` usuwa poprzednie wyjścia i buduje je ponownie. Samo `-Clean` usuwa `build/`, `kurogane.img`, `kurogane.iso`, staged `iso/kernel.elf` i `iso/EFI/BOOT`.

## Wrapper WSL2

```bash
./scripts/build.sh debug
./scripts/build.sh release
./scripts/build.sh rebuild
```

Wrapper wywołuje ten sam `scripts/build.ps1`, więc nie stanowi osobnego systemu budowania.

## GNU Make

Kanoniczny frontend PowerShell można uruchomić także przez:

```bash
make powershell CONFIG=debug
make powershell CONFIG=release
make rebuild
make clean
```

Zwykłe `make CONFIG=debug` ma własny graf zależności dla obiektów kernela, a na końcu zleca PowerShellowi staging. `make verify` drukuje nagłówki i segmenty `kernel.elf`. Repozytoryjny frontend PowerShell pozostaje źródłem prawdy dla bootloadera i obrazu FAT32.

## Konfiguracje

| Konfiguracja | Najważniejsze flagi |
| --- | --- |
| `debug` | `-O0 -g3 -fno-omit-frame-pointer -DKUROGANE_DEBUG=1` |
| `release` | `-O2 -g1 -DNDEBUG -DKUROGANE_DEBUG=0` |

Obie konfiguracje używają C++17, modelu freestanding PIE, `-mno-red-zone`, bez wyjątków, RTTI, stack protectora i kodu zmiennoprzecinkowego/SSE w kernelu. Obie włączają `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wundef -Werror=return-type`.

Zmiana `debug` ↔ `release` nie może korzystać ze starych obiektów: fingerprint obejmuje konfigurację, flagi, listę źródeł, linker script i skróty narzędzi. Zmiana nagłówka przebudowuje obiekty, których aktualność nie może być bezpiecznie zachowana.

## Artefakty

Po zwykłym buildzie powstają:

- `build/kernel.elf` — relokowalny ELF64 PIE kernela;
- `build/kernel.map` — mapa linkera;
- `build/BOOTX64.EFI` — własna aplikacja UEFI PE32+;
- `iso/EFI/BOOT/BOOTX64.EFI` — ścieżka fallback UEFI;
- `iso/kernel.elf` i `iso/EFI/BOOT/kernel.elf` — staged kernel;
- `kurogane.img` — deterministyczny obraz FAT32 o rozmiarze 64 MiB.

Skrypt sprawdza typ i architekturę ELF, brak segmentów RWE, obecność `PT_DYNAMIC`, wyłącznie obsługiwane relokacje `R_X86_64_RELATIVE` i brak niezdefiniowanych symboli. Sprawdza również nagłówki wygenerowanego PE32+ oraz strukturę i zawartość FAT32.

## ISO

W WSL2:

```bash
./scripts/build-iso.sh release
```

Można podać `debug` zamiast `release`; domyślną konfiguracją skryptu ISO jest `release`. Skrypt najpierw uruchamia pełny build PowerShell, następnie umieszcza zweryfikowany `kurogane.img` jako obraz El Torito UEFI i tworzy `kurogane.iso` przez `xorriso`. Nie modyfikuj obrazu ręcznie przed testem — build już umieszcza loader i kernel w wymaganych ścieżkach.

## Przełączniki dla narzędzi

- `-NoStage` kompiluje i weryfikuje kernel bez ponownego budowania loadera, stagingu i obrazu FAT32.
- `-StageOnly` pomija kompilację kernela, ale wymaga istniejącego poprawnego `build/kernel.elf`; buduje loader, wykonuje staging i obraz. Nie wolno łączyć go z `-Clean` ani `-Rebuild`.

Są to przełączniki pomocnicze dla grafu budowania. Do zwykłej pracy używaj pełnego polecenia z `-Configuration`.

## Następny krok

Po buildzie uruchom testy z [TESTING.md](TESTING.md). Sam fakt utworzenia artefaktów nie potwierdza poprawnego rozruchu.
