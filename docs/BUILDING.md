# Budowanie KuroganeOS

## Wymagania

Kanoniczny build działa w Windows PowerShell i używa repozytoryjnego cross toolchainu `tools/compiler/x86_64-elf/bin`. WSL2 z Bash, Pythonem 3, `g++`, `make`, `fsck.fat`, `sgdisk` i `xorriso` obsługuje testy hostowe oraz obrazy/ISO. QEMU znajduje się w `tools/qemu`.

## Jeden punkt wejścia

```powershell
.\scripts\build.ps1 -Configuration debug
.\scripts\build.ps1 -Configuration release
.\scripts\build.ps1 -Configuration test
```

Pełna przebudowa:

```powershell
.\scripts\build.ps1 -Configuration debug -Rebuild
```

`-Rebuild` usuwa odtwarzalne obiekty, staging i obrazy, ale zachowuje `build/logs`, host test binaries/SDK oraz `state/KuroganeOS.img`. Working image może zawierać dane użytkownika. Reset wymaga jawnego:

```powershell
.\scripts\build-foundation-image.ps1 -ResetWorkingImage
```

Build ma lock `state/.build.lock`; druga równoległa instancja kończy się błędem zamiast uszkodzić artefakty.

## Profile i funkcje

| Profil | Przeznaczenie |
| --- | --- |
| `debug` | `-O0`, symbole, assertions i rozszerzona diagnostyka |
| `release` | `-O2`, `NDEBUG`, ograniczone koszty debug |
| `test` | `-O1`, symbole, `KUROGANE_TEST=1` i testy runtime |

Wszystkie profile są freestanding C++17 PIE, bez exceptions/RTTI/red zone/SSE w kernelu. Centralne przełączniki są w `config/features.conf`; build waliduje `y/n` i przekazuje `CONFIG_*` do wszystkich jednostek kernela.

## Wrappery

```bash
./scripts/build.sh debug
./scripts/build.sh release
./scripts/build.sh test
make powershell CONFIG=test
```

Wrappery wywołują ten sam frontend PowerShell. Bezpośredni Makefile służy do wygodnej kompilacji deweloperskiej, ale PowerShell pozostaje źródłem prawdy dla loadera, manifestu i obrazów.

## Artefakty

- `build/kernel.elf` i `build/kernel.map`;
- `build/BOOTX64.EFI`;
- `build/build-info.txt` — profil, compiler/linker, architektura, commit lub `unavailable`, flagi i funkcje;
- `kurogane.img` — deterministyczny 64 MiB FAT32;
- `build/images/KuroganeOS-base.img` — 512 MiB GPT z ESP i root FAT32;
- `state/KuroganeOS.img` — zachowywany dysk roboczy;
- `kurogane.iso` po `./scripts/build-iso.sh release`.

Build odrzuca niepoprawny ELF/PE, segment RWE, nieobsługiwaną relokację, undefined symbol, błędny FAT lub GPT. `-NoStage` kończy na zweryfikowanym kernelu; `-StageOnly` przebudowuje loader/staging/obrazy z istniejącego kernela.

Po buildzie uruchom [pełną regresję](TESTING.md). Sam artefakt nie dowodzi bootu.
