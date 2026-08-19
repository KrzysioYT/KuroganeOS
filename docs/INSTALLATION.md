# Instalacja KuroganeOS 3.3.3-dev — DEV BETA

Ta dokumentacja opisuje bieżący kontrakt Red Flux Setup dla KuroganeOS
3.3.3-dev. Dla VirtualBox używaj również [`VIRTUALBOX.md`](VIRTUALBOX.md).

## Canonical media

```text
VirtualBox:
  dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso

QEMU:
  dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
```

`.iso` jest nośnikiem optycznym x86-64 UEFI dla Oracle VirtualBox. `.img` jest
raw image przeznaczonym dla QEMU. Nie zamieniaj tych mediów między
hypervisorami.

## Try i Install

Red Flux Setup udostępnia dwa tryby:

```text
Try KuroganeOS
  -> read-only live package root
  -> bez destrukcyjnego zapisu na HDD

Install KuroganeOS
  -> konfiguracja użytkownika
  -> wybór dysku
  -> jawne potwierdzenie INSTALL
  -> destrukcyjny deployment na wybrany dysk
```

## Bezpieczeństwo przed wymazaniem dysku

`install.pkg` jest weryfikowany **przed** wyborem destrukcyjnej ścieżki. Runtime
sprawdza:

- magic/version/layout paczki;
- manifest CRC;
- zakres każdego pliku;
- CRC każdego pliku;
- destination `ESP` lub `ROOT`;
- brak duplikatów;
- ten sam bounded FAT 8.3 path contract co build-time package generator.

Poprawny preflight emituje:

```text
[TEST] installer_package_preflight: PASS
```

Pakiet z nazwą, której natywny writer FAT32 nie potrafi utworzyć, ma zostać
odrzucony przed pierwszym zapisem GPT.

## Potwierdzenie destrukcyjnej instalacji

Installer akceptuje m.in.:

```text
install
Install
INSTALL
```

Wpis jest normalizowany do wielkich liter. Błędny tekst pozostawia formularz
aktywny. `Esc` wraca do wyboru dysku.

Dopiero prawidłowe potwierdzenie emituje:

```text
[TEST] installer_confirmation: PASS
```

i zezwala na zmianę zawartości wybranego dysku.

## Pipeline instalacji 1/9 → 9/9

Bieżący installer używa transakcyjnej kolejności inspirowanej ogólnymi
praktykami dojrzałych installerów systemowych. Implementacja jest własna i nie
kopiuje kodu Linux/GNU.

```text
1/9  PARTITIONING TARGET DISK
     protective MBR + primary/backup GPT

2/9  VALIDATING PARTITION TABLE
     ponowny parse GPT + bounded partition devices

3/9  FORMATTING ESP AND ROOT
     ESP FAT32 + Kurogane Root FAT32

4/9  MOUNTING NEW FILESYSTEMS
     świeży mount obu filesystemów

5/9  COPYING ROOT SYSTEM PAYLOAD
     ROOT files + dynamiczne tworzenie katalogów nadrzędnych + sync ROOT

6/9  WRITING USER AND FIRST-BOOT STATE
     locale.cfg + user.cfg + first.run + sync ROOT

7/9  ACTIVATING UEFI BOOT PAYLOAD
     ESP files, w tym BOOTX64.EFI, dopiero po trwałym ROOT + sync ESP

8/9  VERIFYING INSTALLED PAYLOAD
     byte-for-byte readback wszystkich plików ROOT i ESP + final sync

9/9  INSTALLATION COMPLETE
     markery PASS + ekran zakończenia
```

Najważniejsza właściwość tej kolejności: system nie publikuje bootowalnego
`BOOTX64.EFI` przed skopiowaniem i zsynchronizowaniem root payloadu.

## Drzewo katalogów install.pkg

Installer **nie używa już ręcznej, zamkniętej listy katalogów**. Dla każdego
pliku tworzy wszystkie brakujące katalogi nadrzędne w kolejności od root.

Przykład:

```text
/etc/ssl/certs.pem
```

powoduje automatyczne przygotowanie:

```text
/etc
/etc/ssl
```

przed `create()` pliku `certs.pem`.

To naprawia błąd starszego flow 3.3.3-dev, w którym `/etc` istniało, ale
`/etc/ssl` nie było tworzone i kopiowanie kończyło się ogólnym:

```text
PACKAGE COPY FAILED
```

## Diagnostyka kopiowania

Błąd kopiowania nie powinien już kończyć się wyłącznie ogólnym komunikatem.
Serial log podaje indeks pliku, destination, ścieżkę, operację i status FAT32,
np.:

```text
[INSTALL][COPY] file=17 destination=ROOT path=/etc/ssl/certs.pem operation=mkdir-parent status=...
```

Możliwe operacje obejmują:

```text
mkdir-parent
remove-stale
create
write
verify-read
verify-stat
```

Ekran setup może nadal pokazać krótszy komunikat, ale szczegółowa przyczyna ma
być dostępna w COM1/QEMU serial logu.

## Retry po nieudanej instalacji

Po zaakceptowaniu `INSTALL` wybrany dysk jest jawnie przeznaczony do skasowania.
Dlatego właściwy installer używa `prepare_install_target()` i może odtworzyć
GPT również na dysku zawierającym ślady poprzedniej, niedokończonej instalacji.

Oznacza to, że po błędzie w stage 2-8 można:

1. zrestartować VM;
2. ponownie wybrać ten sam VDI;
3. ponownie wpisać `INSTALL`;
4. installer odtworzy layout i filesystemy od początku.

Nie trzeba usuwać i tworzyć VDI ponownie tylko dlatego, że poprzednia próba
zdążyła zapisać GPT.

Konserwatywne `disk_layout::prepare_empty_disk()` nadal istnieje dla testów i
narzędzi wymagających absolutnie pustego LBA0, ale nie jest używane przez
potwierdzoną ścieżkę Red Flux Setup.

## VirtualBox — wymagany storage

```text
Firmware:       EFI64 / UEFI
Secure Boot:    OFF
I/O APIC:       ON
RAM:            2048 MiB
CPU:            1-2
HDD controller: SATA / Intel AHCI
SATA ports:     1 dla pojedynczego VDI
HDD:            VDI >= 2 GiB, SATA 0:0
DVD controller: IDE / PIIX4
DVD:            canonical VirtualBox ISO
Boot order:     DVD -> Disk
Network:        NAT
NIC:            Intel PRO/1000 MT Desktop (82540EM)
Audio:          Intel AC'97
Input:          PS/2
```

Installer zapisuje target przez własny sterownik PCI AHCI. VDI podpięty tylko
przez IDE nie jest wspieranym targetem instalacji.

## Oczekiwane markery pełnej instalacji

Na nowym buildzie spodziewaj się m.in.:

```text
[TEST] installer_package_preflight: PASS
[TEST] installer_confirmation: PASS
installer stage 1/9: target confirmed and GPT written
installer stage 2/9: GPT validated and partition views ready
installer stage 3/9: filesystems formatted
installer stage 4/9: fresh filesystems mounted
installer stage 5/9: root payload copied and synced
installer stage 6/9: profile and first-boot state committed
installer stage 7/9: UEFI payload activated and synced
installer stage 8/9: installed payload verified
[TEST] installer_gpt: PASS
[TEST] installer_filesystems: PASS
[TEST] installer_root_payload: PASS
[TEST] installer_uefi_bootloader: PASS
[TEST] installer_profile: PASS
[TEST] installer_payload_verify: PASS
[TEST] installer_complete: PASS
installer stage 9/9: installation complete
```

## Co zostaje zainstalowane

Pakiet zawiera m.in.:

```text
ESP:
  /EFI/BOOT/BOOTX64.EFI
  /kernel.elf
  /EFI/BOOT/kernel.elf

ROOT:
  /boot/kernel.elf
  /system/init
  /apps/*
  /gui/*
  /etc/system.cfg
  /etc/ssl/certs.pem
  /etc/boot.cfg
```

Installer generuje dodatkowo:

```text
/etc/locale.cfg
/etc/user.cfg
/etc/first.run
```

W DEV BETA credential verifier nadal używa `FNV1A64-DEV`; nie jest to
produkcyjny password KDF.

## Budowanie Windows + WSL

Po zmianach w kernelu/installerze wymagany jest pełny rebuild:

```powershell
.\scripts\build-media.ps1 -Configuration release -Rebuild
```

Canonical VirtualBox ISO:

```text
dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
```

## Realny Oracle VirtualBox qualification

```powershell
.\scripts\smoke-virtualbox-iso.ps1 `
    -Iso ".\dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso" `
    -TimeoutSeconds 180
```

Smoke tworzy tymczasową prawdziwą VM z EFI64, jednoportowym IntelAHCI, VDI,
IDE DVD, E1000 NAT i COM1 serial logiem.

Bieżąca kwalifikacja **nie kończy się już na `kernel entry` ani na samym zapisie
GPT**. PASS wymaga:

```text
[TEST] installer_complete: PASS
```

Dzięki temu regresje formatowania, nested directory creation, package copy,
UEFI activation lub verification mają oblać release build.

## Po udanej instalacji

1. Wyłącz VM.
2. Odłącz ISO albo ustaw HDD przed DVD w kolejności bootowania.
3. Uruchom system z VDI.
4. Sprawdź first-boot/Login i trwałość `/etc`.

Jeżeli po instalacji ponownie uruchamia się Setup, VM nadal bootuje z DVD.