# Instalacja KuroganeOS 2.2

Mechanizm instalacyjny został wprowadzony w 2.1 i pozostaje fundamentem 2.2:
UEFI ISO → SATA/AHCI → GPT → FAT32 → bootloader + kernel + userspace → boot z
persistent HDD.

> **Uwaga:** instalator zapisuje tablicę partycji i formatuje wybrany dysk.
> Używaj go wyłącznie na pustym dysku testowym/nośniku przeznaczonym do skasowania.

## Budowanie ISO

Windows + WSL:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1 -Configuration release
```

Wynik bieżącej wersji:

```text
dist/KuroganeOS-2.2.0-x86_64.iso
dist/SHA256SUMS.txt
```

`kurogane.iso` pozostaje lokalną compatibility copy dla starszych helperów.

## VirtualBox

1. Utwórz nową VM x86-64, np. `KuroganeOS-2.2-Test`.
2. Włącz EFI/UEFI.
3. Przydziel 256–512 MiB RAM i 1 vCPU na pierwszy test.
4. Utwórz nowy pusty VDI.
5. Podłącz go przez SATA / Intel AHCI.
6. Podłącz `dist/KuroganeOS-2.2.0-x86_64.iso` jako DVD.
7. Bootuj z ISO.
8. W instalatorze wybierz wyłącznie testowy VDI.

## Co instaluje system

Instalator przygotowuje protective MBR, primary/backup GPT, ESP FAT32 oraz
persistent KuroganeOS root FAT32. Kopiuje m.in.:

```text
/EFI/BOOT/BOOTX64.EFI
kernel.elf
/system/init
/system/...
/apps/...
/gui/...
/etc/...
/home/...
/var/...
```

Po zapisie następuje verification i flush.

## Boot po instalacji

1. Wyłącz VM.
2. Odłącz ISO.
3. Bootuj z dysku SATA.

Ścieżka:

```text
UEFI
  -> EFI/BOOT/BOOTX64.EFI
  -> kernel.elf
  -> AHCI + GPT
  -> persistent FAT32 root
  -> /system/init (PID 1)
  -> Ring 3 userspace
  -> Flux Console / Desktop Developer Preview
```

Oczekiwane markery obejmują:

```text
[INFO][VFS] persistent FAT32 root mounted read-write
[TEST] userspace_init_spawn: PASS
[INFO][INIT] spawned /system/init as PID 1
[TEST] ALL_REQUIRED_TESTS_PASSED
```

## Persistence

Poprawny start ISO nie jest testem persistence. Wymagany jest boot z tego samego
HDD po odłączeniu ISO i ponowny restart z zachowaniem danych.

## QEMU

Referencyjne logi fundamentu 2.1 pozostają w `build/logs/installer-*-serial.log`.
Po zmianach 2.2 należy wykonać świeży build/test przed deklaracją nowego runtime
PASS.

## Recovery

Safe mode i diagnostics istnieją, ale pełne recovery/update rollback nadal nie
jest ukończone. Zobacz [`RECOVERY.md`](RECOVERY.md).
