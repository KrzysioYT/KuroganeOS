# Instalacja KuroganeOS 3.3.3-dev — DEV BETA

Ta instrukcja opisuje bieżący installer KuroganeOS. Bieżąca gałąź rozwija
Forged Steel/KuroganeOS 5, ale publiczny numer pozostaje `3.3.3-dev`.

> Installer jest destrukcyjny dla wybranego targetu. Używaj pustego wirtualnego
> dysku/test image. Nie kieruj DEV BETA na dysk z ważnymi danymi.

## Media

Windows canonical media:

```text
VirtualBox:
  dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso

QEMU:
  dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
```

VirtualBox używa ISO jako DVD. QEMU używa raw IMG. Nie zamieniaj ich rolami.

## Try / Install

Setup udostępnia:

```text
TRY KUROGANEOS
  -> live/read-only system root
  -> Secure Access
  -> Forged Steel desktop
  -> bez formatowania target HDD

INSTALL KUROGANEOS
  -> locale/profile
  -> wybór targetu
  -> jawne potwierdzenie INSTALL
  -> GPT + ESP + Kurogane Root
  -> install.pkg deployment
```

## install.pkg preflight

Pakiet jest sprawdzany przed destrukcyjnym zapisem. Weryfikacja obejmuje m.in.:

- magic/version/layout;
- manifest CRC;
- bounds plików;
- CRC plików;
- destination `ESP`/`ROOT`;
- duplikaty;
- FAT 8.3-safe package paths.

Poprawny preflight:

```text
[TEST] installer_package_preflight: PASS
```

### FAT 8.3 contract

Ścieżki package payload muszą spełniać kontrakt writer-a installera. Dlatego
Anvil config używa:

```text
/etc/anvil.cfg
```

Stare `/etc/anvil.repo` jest niedozwolone, bo `.repo` ma czteroznakowe
rozszerzenie.

## Potwierdzenie destrukcyjnej operacji

Installer akceptuje case-insensitive:

```text
install
Install
INSTALL
```

Błędny tekst nie zapisuje GPT i pozwala spróbować ponownie. `Esc` wraca do
wyboru dysku.

Po poprawnym potwierdzeniu:

```text
[TEST] installer_confirmation: PASS
```

## Pipeline 1/9 -> 9/9

```text
1/9  PARTITIONING TARGET DISK
     protective MBR + primary/backup GPT

2/9  VALIDATING PARTITION TABLE
     ponowny parse GPT + bounded partition views

3/9  FORMATTING ESP AND ROOT
     ESP FAT32 + Kurogane Root FAT32

4/9  MOUNTING NEW FILESYSTEMS
     mount świeżych filesystemów

5/9  COPYING ROOT SYSTEM PAYLOAD
     rootfs + dynamiczne parent directories + sync ROOT

6/9  WRITING USER AND FIRST-BOOT STATE
     locale.cfg + user.cfg + first.run + sync ROOT

7/9  ACTIVATING UEFI BOOT PAYLOAD
     BOOTX64.EFI i ESP dopiero po trwałym ROOT

8/9  VERIFYING INSTALLED PAYLOAD
     byte-for-byte readback ROOT/ESP + sync

9/9  INSTALLATION COMPLETE
     final PASS markers
```

Najważniejsza własność: bootloader na ESP jest aktywowany **po** zapisaniu i
zsynchronizowaniu system root.

## Parent directories

Installer tworzy katalogi nadrzędne dynamicznie na podstawie manifestu. Jest to
konieczne dla ścieżek typu:

```text
/etc/ssl/certs.pem
```

Brak ręcznie wpisanego `/etc/ssl` nie może już powodować `PACKAGE COPY FAILED`.

## Profil użytkownika

Installer zapisuje lokalne dane m.in. do:

```text
/etc/locale.cfg
/etc/user.cfg
```

Po pierwszym boot:

```text
UEFI
 -> kernel
 -> persistent Kurogane Root
 -> /system/init PID 1
 -> KUROGANE // SECURE ACCESS
 -> profil lokalny
 -> Blade Launcher / Forged Steel desktop
```

Bieżący DEV credential hash nie jest produkcyjnym KDF. Nie używaj ważnego
hasła.

## VirtualBox

Target VDI musi być podpięty przez:

```text
SATA / Intel AHCI
```

Referencyjny profil sieci VirtualBox to obecnie:

```text
PCnet-FAST III (Am79C973) + NAT
```

Pełny profil: [VIRTUALBOX.md](VIRTUALBOX.md).

## QEMU installer test

Automatyczny installer używa wyłącznie disposable image pod
`build/test-disks/`.

Przykładowy target należy najpierw utworzyć odpowiednim helperem testowym, a
następnie uruchomić canonical runner z `-InstallerTest`. Nie podawaj fizycznego
dysku.

## Oczekiwane markery

```text
[TEST] installer_package_preflight: PASS
[TEST] installer_confirmation: PASS
installer stage 1/9: ...
installer stage 2/9: ...
installer stage 3/9: ...
installer stage 4/9: ...
installer stage 5/9: ...
installer stage 6/9: ...
installer stage 7/9: ...
installer stage 8/9: ...
[TEST] installer_gpt: PASS
[TEST] installer_filesystems: PASS
[TEST] installer_root_payload: PASS
[TEST] installer_uefi_bootloader: PASS
[TEST] installer_profile: PASS
[TEST] installer_payload_verify: PASS
[TEST] installer_complete: PASS
installer stage 9/9: installation complete
```

## Retry po nieudanej instalacji

Bieżący flow może ponownie przygotować ten sam wybrany **testowy** VDI po
kolejnym świadomym `INSTALL`. Nie jest to mechanizm odzyskiwania danych — target
jest traktowany jako przeznaczony do ponownego deploymentu.

## Realny VirtualBox install smoke

```powershell
.\scripts\smoke-virtualbox-iso.ps1 `
  -Iso .\dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso `
  -TimeoutSeconds 180
```

Finalny PASS powinien obejmować:

```text
optical UEFI boot
AHCI target
installer_complete
poweroff/reboot
boot z VDI bez ISO
persistent Kurogane Root
/system/init PID 1
Secure Access
canonical VirtualBox network path
```

Sam marker `kernel entry` albo samo istnienie ISO nie jest wystarczające.

## Diagnostyka

### `no PCI AHCI controller`

VM wystartowała, ale target nie jest na SATA/AHCI.

### `path is outside the installer's FAT 8.3 contract`

Payload zawiera niedozwoloną nazwę. Popraw nazwę w source/rootfs/build scripts;
nie osłabiaj walidatora tylko po to, żeby paczka przeszła.

### `PACKAGE COPY FAILED`

Sprawdź serial lines `[INSTALL][COPY]` — zawierają destination, path, operation i
status.

### Verify zatrzymuje się na pliku

Szukaj:

```text
[INSTALL][VERIFY] file=... destination=... path=... bytes=...
[INSTALL][VERIFY] PASS path=...
```

## Po instalacji

Po odłączeniu ISO system powinien bootować z VDI/HDD do Secure Access. Jeżeli
zatrzymuje się przed loginem, sprawdź serial pod kątem:

```text
persistent FAT32 root mounted read-write
[TEST] userspace_init_spawn: PASS
/system/init: PID 1 online
[TEST] userspace_init_pid1: PASS
[TEST] kurogane5_obsidian_login: PASS
```
