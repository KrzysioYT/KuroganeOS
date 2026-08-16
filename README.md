# KuroganeOS 3.3.0-dev — DEV BETA

KuroganeOS jest edukacyjnym, 64-bitowym systemem operacyjnym rozwijanym od
podstaw dla x86-64 i UEFI. **Nie używa kernela Linux.** Wersja `3.3.0-dev` jest
wydaniem **DEV BETA** przeznaczonym do testów, rozwoju i instalacji na
kontrolowanych maszynach wirtualnych/dyskach testowych.

## 3.3 — jeden nośnik, dwa tryby

Od 3.3 zarówno wersjonowany IMG, jak i ISO zawierają pakiet instalacyjny i
uruchamiają Red Flux Setup:

```text
UEFI
 -> KuroganeOS 3.3 DEV BETA
 -> Red Flux Setup
    -> Try KuroganeOS
       -> read-only live root
       -> Login
       -> Red Flux Home
    -> Install KuroganeOS
       -> English / Polski
       -> nazwa użytkownika
       -> konto bez hasła / z hasłem
       -> wybór dysku SATA/AHCI
       -> potwierdzenie INSTALL
       -> GPT + ESP + Kurogane Root
       -> pierwszy boot z zainstalowanego dysku
```

`Try KuroganeOS` nie wymaga instalacji: kernel montuje `install.pkg` jako
read-only live VFS i uruchamia zwykły Ring-3 desktop. `Install KuroganeOS`
korzysta z istniejącego kernelowego backendu GPT/FAT32 i nie wystawia surowego
partycjonowania aplikacjom Ring 3.

> [!WARNING]
> Instalator **kasuje i ponownie partycjonuje wybrany dysk** dopiero po jawnym
> wpisaniu `INSTALL`. Używaj osobnego pustego dysku/VDI/QEMU drive.

## Konto i Login

Instalator zapisuje:

```text
/etc/locale.cfg
/etc/user.cfg
/etc/first.run
```

Obsługiwane profile językowe to `en-US` i `pl-PL`. Konto może działać bez hasła
albo wymagać hasła na ekranie Login.

> [!CAUTION]
> W DEV BETA verifier hasła używa tymczasowego `FNV1A64-DEV`. To **nie jest
> kryptograficzny KDF ani produkcyjny credential store**. Nie używaj hasła,
> którego używasz gdziekolwiek indziej.

## Red Flux Desktop

Aktualny desktop zawiera:

- boot splash i Red Flux Setup/Login;
- Red Flux Home jako root sesji;
- Dock z Home, Terminal, Files, Monitor, Settings i About;
- focus, z-order, drag, resize, minimize/maximize/restore/close;
- software backbuffer + damage-style GOP scanout;
- czarno-grafitowo-czerwoną identyfikację wizualną;
- wspólny `FluxShellCore` dla recovery shell i GUI Terminala;
- Ring-3 procesy, prywatne address spaces i syscall ABI;
- AHCI, GPT oraz writable FAT32/VFS dla zainstalowanego systemu.

## Budowanie media — zalecany workflow

Od 3.3 **nie buduj osobno IMG i ISO**, jeśli chcesz pełny nośnik testowo-
instalacyjny. Użyj jednej z poniższych komend `build-media`.

### Windows 11 + WSL

> [!IMPORTANT]
> Windows wymaga dodatkowych plików build toolchainu, których nie ma w Git.
> Bez nich kernel, SDK, IMG i ISO nie zostaną zbudowane.
>
> Pobierz paczkę:
> **[KuroganeOS — wymagane pliki build dla Windows](https://drive.google.com/file/d/1sHfNdDOOVeJh3Q0FOtUlqPbHZIZ-ykEk/view?usp=sharing)**
>
> Wypakuj jej zawartość do **głównego katalogu repozytorium**, zachowując
> strukturę. Wymagany jest m.in. `tools/compiler/x86_64-elf/bin/`.

Pełny DEV BETA media build:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-media.ps1 -Configuration release -Rebuild
```

### macOS

Pierwsze przygotowanie:

```bash
chmod +x scripts/*.sh
./scripts/setup-macos.sh --install
```

Pełny IMG + ISO:

```bash
bash ./scripts/build-media-macos.sh --configuration release --rebuild
```

Apple Silicon uruchamia x86-64 KuroganeOS przez QEMU TCG.

### Linux x86-64

Pierwsze przygotowanie (apt/dnf/pacman):

```bash
bash ./scripts/setup-linux.sh --install
```

Pełny IMG + ISO:

```bash
bash ./scripts/build-media-linux.sh --configuration release --rebuild
```

Na x86-64 Linux build może użyć natywnego GNU `gcc/g++/binutils` w trybie
freestanding. Jeśli `x86_64-elf-*` jest dostępny, skrypty preferują dedykowany
cross-toolchain.

## Artefakty 3.3 DEV BETA

Host-specific QEMU IMG:

```text
dist/KuroganeOS-3.3.0-dev-windows-qemu.img
dist/KuroganeOS-3.3.0-dev-macos-qemu.img
dist/KuroganeOS-3.3.0-dev-linux-qemu.img
```

Wspólny ISO:

```text
dist/KuroganeOS-3.3.0-dev-x86_64.iso
dist/SHA256SUMS.txt
```

Oba typy nośnika mają wejść do `Try / Install` setupu.

## Sterowanie

- mysz — focus, okna i Dock;
- strzałki — wybór/nawigacja;
- `Enter` — zatwierdzenie;
- `Escape` — anulowanie lokalnej akcji;
- `Tab` — następny element tam, gdzie jest obsługiwany;
- `Alt+Tab` — przełączanie okien;
- `Alt+F4` — zamknięcie aktywnego okna.

Installer używa strzałek + Enter. Przed rozpoczęciem zapisu na dysk wymaga
wpisania dokładnego słowa `INSTALL`.

## Tryby awaryjne

- normalny boot nośnika z `install.pkg` — Red Flux Setup;
- normalny boot zainstalowanego systemu — Boot Splash -> Login -> Desktop;
- `S` / `F8` — Safe Mode;
- `X` — Diagnostics.

## Status DEV BETA

3.3 jest przeznaczone do aktywnego testowania. Pełny runtime acceptance wymaga
jeszcze świeżych testów IMG/ISO, instalacji i rebootu na docelowych hostach.
Nie traktuj oznaczenia DEV BETA jako deklaracji gotowości produkcyjnej.

Szczegóły: [`docs/releases/3.3.0-dev.md`](docs/releases/3.3.0-dev.md).

## Dokumentacja

- [`docs/releases/3.3.0-dev.md`](docs/releases/3.3.0-dev.md)
- [`docs/INSTALLATION.md`](docs/INSTALLATION.md)
- [`docs/roadmap/DESKTOP_ROADMAP.md`](docs/roadmap/DESKTOP_ROADMAP.md)
- [`docs/GUI.md`](docs/GUI.md)
- [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md)
- [`docs/MACOS_DEVELOPMENT.md`](docs/MACOS_DEVELOPMENT.md)

## Licencja

Aktualne rewizje KuroganeOS są udostępniane na warunkach **KuroganeOS
Source-Available License 1.0 (KSAL-1.0)**. Nie jest to licencja Open Source
zatwierdzona przez OSI.

- [`LICENSE`](LICENSE)
- [`LICENSE-MIT-LEGACY`](LICENSE-MIT-LEGACY)
- [`docs/LICENSING.md`](docs/LICENSING.md)
- [`CLA.md`](CLA.md)
- [`CONTRIBUTING.md`](CONTRIBUTING.md)
