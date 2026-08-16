# KuroganeOS Developer Documentation

Ta sekcja jest punktem startowym dla osób, które chcą pisać programy albo
rozwijać KuroganeOS.

Jeżeli chcesz tylko uruchomić system, zacznij od:

[`../START_HERE.md`](../START_HERE.md)

## Co mogę rozwijać?

Masz trzy główne poziomy:

### 1. Aplikacje Ring-3 — najlepszy start

Piszesz zwykły program C/C++, który działa jako proces użytkownika.

Czytaj:

- [`APP_DEVELOPMENT.md`](APP_DEVELOPMENT.md)
- [`API_REFERENCE.md`](API_REFERENCE.md)
- [`GUI_APPLICATIONS.md`](GUI_APPLICATIONS.md)

### 2. Biblioteki i SDK

Rozwijasz `libc`, `libkurogane`, `libui`, nagłówki publiczne albo narzędzia
build.

Czytaj:

- [`API_REFERENCE.md`](API_REFERENCE.md)
- katalog `sdk/`
- katalog `userspace/runtime/`

### 3. Kernel / sterowniki / filesystem / scheduler

To poziom dla osób, które rozumieją x86-64, UEFI, pamięć wirtualną, przerwania i
izolację Ring-3.

Czytaj:

- [`KERNEL_CONTRIBUTION.md`](KERNEL_CONTRIBUTION.md)
- [`../ARCHITECTURE.md`](../ARCHITECTURE.md)
- [`../INSTALLATION.md`](../INSTALLATION.md)

## Najważniejsza zasada projektu

Nie rozwiązuj brakującego API użytkownika przez dodanie syscalla w stylu:

```text
execute_arbitrary_kernel_command(string)
```

To niszczy granicę Ring-3/Ring-0.

Zamiast tego dodawaj mały, walidowany kontrakt:

```text
application
 -> SDK wrapper
 -> syscall number
 -> kernel validation
 -> subsystem
```

## Gdzie są przykłady?

```text
sdk/examples/
userspace/apps/
userspace/gui/
```

## Jak zbudować SDK?

### macOS / Linux

```bash
bash ./scripts/build-sdk.sh
```

### Windows

Windows wymaga repozytoryjnego toolchainu opisanego w `../START_HERE.md`.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-sdk.ps1
```

## Docelowy format aplikacji

Aktualnie aplikacje użytkownika to:

```text
ELF64
architecture: x86-64
Ring: 3
static/freestanding
custom Kurogane syscall ABI
```

Nie są to pliki `.exe` Windows i nie są to binaria Linux.

## Co przeczytać jako pierwsze?

Jeżeli nie wiesz, wybierz tę kolejność:

1. `APP_DEVELOPMENT.md`
2. skompiluj Hello World;
3. uruchom go w KuroganeOS;
4. `GUI_APPLICATIONS.md`;
5. dopiero potem `API_REFERENCE.md`;
6. kernel zostaw na koniec.
