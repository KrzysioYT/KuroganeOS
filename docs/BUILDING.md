# Budowanie KuroganeOS

Wykonuj polecenia z katalogu repozytorium. Wersja pochodzi z
`common/version.h`, obecnie `7.0.0-dev`; nazwa Flux 8 określa cel rozwoju.

```bash
git submodule update --init --recursive
```

## Windows 11 + WSL

PowerShell używa cross GCC/binutils z `tools/compiler/x86_64-elf/bin`.
Wymagane są m.in. gcc, g++, ld, objcopy, readelf i objdump z prefiksem
`x86_64-elf-`. WSL zapewnia narzędzia budowania obrazów; sprawdź wymagania
w [Linux development](LINUX_DEVELOPMENT.md). Toolchain nie jest częścią Git.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-media.ps1 -Configuration release -Rebuild
```

Bieżące wyjścia Windows:

```text
dist/KuroganeOS-7.0.0-dev-virtualbox-x86_64.iso
dist/KuroganeOS-7.0.0-dev-qemu-x86_64.img
```

## Linux

```bash
bash scripts/setup-linux.sh --install
bash scripts/build-media-linux.sh --configuration release --rebuild
```

## macOS

```bash
bash scripts/setup-macos.sh --install
bash scripts/build-media-macos.sh --configuration release --rebuild
```

Linux/macOS publikują `dist/KuroganeOS-7.0.0-dev-x86_64.iso` oraz wariant
`dist/KuroganeOS-7.0.0-dev-<host>-qemu.img`. Zawsze czytaj ścieżkę wypisaną
przez swój builder; nie mieszaj nazw hostów ani starych obrazów.

## Build bez obrazu i walidacja

```bash
bash scripts/build-linux.sh --configuration release --no-image
python3 tests/test_userspace_build_parity.py
bash scripts/run-host-tests.sh
```

Sama kompilacja nie potwierdza bootu. Windows i Linux mają wspólny kontrakt
33 programów bazowego userspace; SDK dodaje aplikacje GUI. Test zgodności
wykrywa pominięte usługi PID 1, błędne ścieżki i rozbieżności flag scalar.
Obrazy ELF są sprawdzane pod kątem architektury, segmentów i zakazanych
rejestrów SIMD. [Testy runtime](TESTING.md).

`-Rebuild` służy do odtworzenia artefaktów. Nie resetuj danych roboczych,
VDI ani profilu konta w ramach zwykłej przebudowy. Zapisz `git rev-parse HEAD`
i SHA256 dokładnie tego obrazu, który uruchamiasz.
