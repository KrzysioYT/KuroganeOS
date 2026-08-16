# System budowania

## Punkt wejścia

Źródłem prawdy jest:

```powershell
.\scripts\build.ps1 -Configuration debug
.\scripts\build.ps1 -Configuration release
.\scripts\build.ps1 -Configuration test
```

`scripts/build.sh` i cele Makefile są wrapperami. Build używa repozytoryjnego cross GCC dla x86-64 ELF i WSL2 do narzędzi obrazów/ISO. Nie jest wymagana migracja na LLVM, ponieważ aktualny toolchain jest stabilny.

## Profile

- `debug`: symbole, assertions i diagnostyka.
- `release`: optymalizacja oraz ograniczone informacje debug.
- `test`: markery testowe kernela i rozszerzona walidacja runtime.

Feature flags pochodzą wyłącznie z `config/features.conf` i są przekazywane jako `CONFIG_*`. Nieznana wartość lub brak wymaganej flagi przerywa build.

## Artefakty

- `build/kernel.elf`, `build/kernel.map`, `build/BOOTX64.EFI`;
- `build/build-info.txt` z profilem, wersjami narzędzi, flagami i funkcjami;
- `kurogane.img` — recovery FAT32;
- `build/images/KuroganeOS-base.img` — odtwarzalny dysk GPT;
- `state/KuroganeOS.img` — zachowywana kopia robocza;
- `kurogane.iso` — obraz release/live.

Build lock w `state/.build.lock` blokuje równoległe modyfikowanie artefaktów. `-Rebuild` usuwa tylko odtwarzalne wyjścia kernela/obrazów i zachowuje logi oraz dane robocze. Working image nie jest resetowany bez jawnego `-ResetWorkingImage`.

## Walidacja

Build sprawdza architekturę ELF, PIE, brak RWE, dozwolone relokacje, undefined symbols, PE32+, FAT32 oraz primary/backup GPT. Pełna macierz jest opisana w [TESTING.md](TESTING.md), a instrukcje operatorskie w [BUILDING.md](BUILDING.md).
