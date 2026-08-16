# Testowanie KuroganeOS 2.2 w VirtualBox

`scripts/run-virtualbox.ps1` automatyzuje odizolowany smoke test UEFI. Tworzy
własną tymczasową VM, konfiguruje EFI64 i Intel AHCI, podłącza `kurogane.iso`,
kieruje COM1 do pliku i sprząta wyłącznie VM utworzoną przez siebie.

Helper nie automatyzuje całego interaktywnego installera, więc sam smoke test
ISO nie dowodzi install → remove ISO → HDD boot → persistence.

## Artefakt

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1 -Configuration release
```

```text
dist/KuroganeOS-2.2.0-x86_64.iso
dist/SHA256SUMS.txt
kurogane.iso   # compatibility copy
```

## Smoke test

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-virtualbox.ps1
```

Opcjonalnie:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-virtualbox.ps1 -TimeoutSeconds 90 -MemoryMiB 512 -KeepOnFailure
```

Helper ustawia EFI64, ICH9/IO-APIC, PS/2 keyboard/mouse, Intel AHCI, COM1 i
kontrolowane ścieżki tymczasowych plików. Odrzuca fatal/panic/triple-fault oraz
wymagany test FAIL.

## Manual acceptance 2.2

1. Świeża VM x86-64 z EFI.
2. 256–512 MiB RAM.
3. Pusty VDI na SATA/Intel AHCI.
4. Podłącz `dist/KuroganeOS-2.2.0-x86_64.iso`.
5. Boot i instalacja wyłącznie na testowym VDI.
6. Wyłącz VM po verification.
7. Odłącz ISO.
8. Bootuj z VDI.
9. Zweryfikuj persistent root i `/system/init` jako PID 1.
10. Dla Desktop Developer Preview uruchom tryb desktop i sprawdź Flux surfaces,
    pointer/focus/drag oraz aplikacje Ring 3.
11. W Terminal/Flux Console sprawdź `version`, `apps`, `run`, `gui` i `jobs`.
12. Zrestartuj ponownie i potwierdź persistence.

Oczekiwane markery kernela:

```text
[INFO][VFS] persistent FAT32 root mounted read-write
[TEST] userspace_init_spawn: PASS
[INFO][INIT] spawned /system/init as PID 1
[TEST] ALL_REQUIRED_TESTS_PASSED
```

## Czego test nie dowodzi

- szerokiej zgodności real hardware UEFI;
- pełnego recovery;
- wszystkich opcjonalnych urządzeń/USB/audio;
- świeżego PASS bez faktycznego uruchomienia builda i VM.

Instrukcja instalacji: [INSTALLATION.md](INSTALLATION.md).
