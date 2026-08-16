# Instalacja KuroganeOS 2.1

KuroganeOS 2.1 jest pierwszym wydaniem projektu przygotowanym wokół pełnego scenariusza instalacyjnego: UEFI ISO → SATA/AHCI → GPT → FAT32 → bootloader + kernel + userspace → boot z persistent HDD.

> **Uwaga:** instalator zapisuje tablicę partycji i formatuje wybrany dysk. Używaj go wyłącznie na pustym wirtualnym dysku lub nośniku przeznaczonym do skasowania. Projekt pozostaje eksperymentalny i nie jest jeszcze przeznaczony do instalacji obok ważnych danych.

## Budowanie ISO

Na referencyjnym środowisku Windows + WSL uruchom:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1 -Configuration release
```

Wymagany jest działający repozytoryjny toolchain oraz WSL z `xorriso`, którego używa skrypt budowania ISO.

Po udanym buildzie powstają:

```text
dist/KuroganeOS-2.1-x86_64.iso
dist/SHA256SUMS.txt
```

Dodatkowo generowany jest lokalny `kurogane.iso` używany przez starsze helpery emulatorów. Nie jest on kanonicznym artefaktem wydania.

## VirtualBox — konfiguracja VM

Zalecany pierwszy test wykonuj na nowej, osobnej maszynie wirtualnej.

1. Utwórz nową maszynę x86-64, np. `KuroganeOS-2.1-Test`.
2. Włącz firmware **EFI/UEFI**.
3. Przydziel co najmniej **256 MiB RAM**; 512 MiB jest wygodną wartością testową.
4. Użyj jednego vCPU na pierwszy test.
5. Utwórz **nowy pusty VDI**. 256 MiB wystarcza dla obecnego małego obrazu testowego; większy dysk jest bezpieczniejszy do dalszej pracy.
6. Podłącz dysk przez kontroler **SATA / Intel AHCI**.
7. Podłącz `dist/KuroganeOS-2.1-x86_64.iso` jako napęd optyczny.
8. Ustaw pierwszy boot z DVD/ISO.
9. Używaj wejścia PS/2 podczas pierwszych testów.

Nie podłączaj do instalatora fizycznego dysku hosta ani VDI zawierającego ważne dane.

## Instalacja

Po uruchomieniu instalacyjnego ISO:

1. Bootloader UEFI ładuje kernel i `install.pkg`.
2. Kernel uruchamia installer mode.
3. Instalator enumeruje dostępne urządzenia SATA/AHCI.
4. Wybierz wyłącznie pusty dysk przeznaczony dla KuroganeOS.
5. Potwierdź operację destrukcyjną dopiero po sprawdzeniu rozmiaru/urządzenia.
6. Instalator przygotowuje protective MBR, primary GPT oraz backup GPT.
7. Tworzona i formatowana jest EFI System Partition FAT32.
8. Tworzony i formatowany jest persistent KuroganeOS root FAT32.
9. Instalowane są co najmniej:

```text
/EFI/BOOT/BOOTX64.EFI
kernel.elf
/system/init
/system/...
/apps/...
/etc/...
/home/...
/var/...
```

10. Instalator weryfikuje zapisane elementy i wykonuje flush.
11. Dopiero po pozytywnej weryfikacji instalacja może zostać uznana za zakończoną.

## Pierwszy boot z HDD

Po zakończeniu instalacji:

1. Wyłącz VM.
2. Odłącz ISO od napędu optycznego.
3. Ustaw boot z dysku SATA.
4. Uruchom VM ponownie.

Prawidłowa ścieżka startowa wygląda następująco:

```text
VirtualBox UEFI
  -> EFI/BOOT/BOOTX64.EFI
  -> kernel.elf
  -> AHCI disk discovery
  -> GPT
  -> persistent FAT32 root
  -> /system/init
  -> PID 1
  -> Ring 3 userspace
  -> userspace console
```

W logu szeregowym oczekuj między innymi:

```text
[INFO][VFS] persistent FAT32 root mounted read-write
[TEST] userspace_init_spawn: PASS
[INFO][INIT] spawned /system/init as PID 1
[TEST] ALL_REQUIRED_TESTS_PASSED
```

Globalny `ALL_REQUIRED_TESTS_PASSED` powinien pojawiać się dopiero po udanym required teście PID 1.

## Persistence

Kod 2.1 posiada probe zapisu trwałego FAT32. Test instalacyjny powinien potwierdzić, że dane zapisane na root filesystem pozostają dostępne po restarcie.

Nie uznawaj samego poprawnego startu ISO za dowód persistence — wymagany jest drugi boot z tego samego dysku.

## QEMU

QEMU + EDK2 pozostaje referencyjnym środowiskiem automatyzacji. Repozytorium zawiera helpery QEMU oraz commitowane logi instalacyjne:

```text
build/logs/installer-first-boot-serial.log
build/logs/installer-deploy-serial.log
build/logs/installer-second-boot-serial.log
```

Logi te pokazują kolejno boot nośnika/pakietu, deployment oraz boot persistent systemu.

## VirtualBox helper

`scripts/run-virtualbox.ps1` tworzy odizolowaną tymczasową VM, konfiguruje EFI i Intel AHCI oraz zapisuje serial. Helper jest przede wszystkim smoke-testem i narzędziem diagnostycznym; nie należy traktować go jako dowodu pełnej automatycznej instalacji, dopóki interakcja installera nie zostanie automatycznie przeprowadzona i sprawdzona.

Więcej: [`VIRTUALBOX_TESTING.md`](VIRTUALBOX_TESTING.md).

## Recovery / safe mode

KuroganeOS ma safe mode i diagnostics, ale pełny recovery environment z automatyczną naprawą filesystemu/rollbackiem nie jest jeszcze ukończony. Szczegóły: [`RECOVERY.md`](RECOVERY.md).
