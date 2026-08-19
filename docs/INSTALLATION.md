# Instalacja KuroganeOS 3.3.3-dev — DEV BETA

Jeżeli pierwszy raz uruchamiasz KuroganeOS, zacznij od:

- [`START_HERE.md`](START_HERE.md)
- [`VIRTUALBOX.md`](VIRTUALBOX.md)

## Media są rozdzielone według hypervisora

Bieżący Windows build publikuje dwa canonical artefakty:

```text
VirtualBox:
  dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso

QEMU:
  dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
```

Nie używaj `.img` jako napędu optycznego VirtualBox i nie traktuj ISO jako
zamiennika raw QEMU IMG.

Oba media prowadzą do Red Flux Setup:

```text
boot media
  -> Red Flux Setup
     -> Try KuroganeOS
     -> Install KuroganeOS
```

> [!WARNING]
> `Install KuroganeOS` zapisuje GPT i formatuje wybrany dysk. Testuj tę ścieżkę
> wyłącznie na pustym wirtualnym dysku lub nośniku przeznaczonym do skasowania.

## Try KuroganeOS

Try uruchamia system bez destrukcyjnego zapisu na dysk. `install.pkg` jest
używany jako live root, z którego startują `/system/init`, Login, Red Flux Home
i aplikacje Ring-3.

## Install KuroganeOS

Wizard prowadzi przez:

1. język `English` / `Polski`;
2. nazwę lokalnego użytkownika;
3. tryb bez hasła albo hasło testowe;
4. wybór dysku SATA/AHCI;
5. potwierdzenie destrukcyjnej instalacji;
6. protective MBR + primary/backup GPT;
7. ESP FAT32 i Kurogane Root FAT32;
8. kopiowanie bootloadera, kernela, userspace i konfiguracji;
9. verification + sync;
10. ekran zakończenia instalacji.

W DEV BETA mechanizm hasła nadal używa `FNV1A64-DEV`. Nie jest to produkcyjny
credential store ani password KDF. Nie używaj w instalacji testowej hasła,
którego używasz gdzie indziej.

## Potwierdzenie `INSTALL`

Potwierdzenie zostało poprawione tak, aby nie powodowało fałszywego fatal error.

Akceptowane są między innymi:

```text
install
Install
INSTALL
```

Installer normalizuje litery do wielkich liter przed porównaniem.

Błędny tekst nie zatrzymuje instalacji. Formularz wyświetla komunikat i pozwala
poprawić wpis. `Esc` wraca bezpiecznie do wyboru dysku.

Do momentu prawidłowego potwierdzenia **żaden GPT nie jest zapisywany**.
Pierwszy destrukcyjny etap rozpoczyna się dopiero po:

```text
[TEST] installer_confirmation: PASS
```

## VirtualBox — wymagany storage

Instalator 3.3.3-dev korzysta z własnego sterownika PCI AHCI. Referencyjny układ:

```text
Controller: SATA / Intel AHCI
└── KuroganeOS.vdi        (SATA Port 0)

Controller: IDE / PIIX4
└── KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
```

Jeżeli dysk VDI jest podpięty przez IDE, ISO może wystartować poprawnie, ale
instalator zakończy się:

```text
[FATAL][INSTALL][CPU0][KERNEL] no PCI AHCI controller
```

To nie jest błąd ISO. Przenieś HDD na SATA / Intel AHCI albo użyj aktualnego
`repair-virtualbox-boot.ps1`.

## Referencyjna VM VirtualBox

```text
Firmware:       EFI64 / UEFI
Secure Boot:    OFF
I/O APIC:       ON
RAM:            2048 MiB
CPU:            1-2
HDD:            SATA / Intel AHCI, pusty VDI >= 2 GiB
DVD:            IDE / PIIX4, canonical VirtualBox ISO
Boot order:     DVD -> Disk
Network:        NAT
NIC:            Intel PRO/1000 MT Desktop (82540EM)
Audio:          Intel AC'97
Input:          PS/2
```

Automatyczne utworzenie VM na Windows:

```powershell
.\scripts\create-virtualbox-vm.ps1 `
    -Iso ".\dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso" `
    -Name "KuroganeOS-3.3.3-VB"
```

Naprawa istniejącej VM:

```powershell
.\scripts\repair-virtualbox-boot.ps1 `
    -Name "KuroganeOS" `
    -Iso ".\dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso"
```

Repair helper wymusza EFI64, sprawdza/zakłada IntelAHCI i potrafi bezpiecznie
przenieść istniejący wirtualny HDD z IDE na SATA 0:0 z próbą rollbacku, jeżeli
podpięcie pod AHCI nie powiedzie się.

## Instalacja w VirtualBox krok po kroku

1. VM musi być całkowicie wyłączona podczas zmian storage.
2. Ustaw EFI64, I/O APIC i Secure Boot OFF.
3. Podepnij pusty VDI jako SATA / Intel AHCI.
4. Podepnij canonical VirtualBox ISO jako IDE DVD.
5. Uruchom VM i wybierz `INSTALL KUROGANEOS`.
6. Wybierz język, username i tryb hasła.
7. Wybierz wyłącznie pusty dysk SATA/AHCI.
8. Sprawdź model i rozmiar dysku.
9. Wpisz `INSTALL` (wielkość liter nie ma znaczenia).
10. Poczekaj na `[TEST] installer_complete: PASS`.
11. Wyłącz VM.
12. Odłącz ISO.
13. Ustaw HDD jako pierwszy boot device.
14. Uruchom VM ponownie i sprawdź Login.

## Co instaluje system

Między innymi:

```text
/EFI/BOOT/BOOTX64.EFI
/boot/kernel.elf
/system/init
/apps/*
/gui/*
/etc/system.cfg
/etc/locale.cfg
/etc/user.cfg
/etc/first.run
```

## Oczekiwane markery instalatora

Po poprawnym potwierdzeniu i instalacji:

```text
[TEST] installer_confirmation: PASS
[TEST] installer_gpt: PASS
[TEST] installer_filesystems: PASS
[TEST] installer_uefi_bootloader: PASS
[TEST] installer_profile: PASS
[TEST] installer_complete: PASS
```

Jeżeli użytkownik naciska `Esc` na ekranie destrukcyjnego potwierdzenia:

```text
[TEST] installer_cancel_safe: PASS
```

i installer wraca do wyboru dysku bez zapisu GPT.

## Budowanie na Windows 11 + WSL

Standardowy pełny build:

```powershell
.\scripts\build-media.ps1 -Configuration release -Rebuild
```

Na Windows realny Oracle VirtualBox smoke jest domyślną częścią
`build-media.ps1`. Smoke można wyłączyć tylko jawnie:

```powershell
.\scripts\build-media.ps1 `
    -Configuration release `
    -Rebuild `
    -SkipVirtualBoxSmoke
```

Build z `-SkipVirtualBoxSmoke` nie powinien być traktowany jako runtime-qualified
VirtualBox release.

## Realny smoke VirtualBox

Można uruchomić osobno:

```powershell
.\scripts\smoke-virtualbox-iso.ps1 `
    -Iso ".\dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso" `
    -TimeoutSeconds 90
```

Smoke tworzy tymczasową VM z:

```text
EFI64
SATA / IntelAHCI HDD
IDE / PIIX4 DVD
E1000 / NAT
COM1 serial log
```

`VBoxManage.exe` jest wywoływany przez `System.Diagnostics.Process`, więc
normalny progress `0%...100%` na stderr nie jest błędnie klasyfikowany jako
PowerShell `NativeCommandError`.

Smoke wymaga aktywnego AHCI i odrzuca log zawierający `[FATAL][INSTALL]`.

## Statyczna walidacja ISO

```bash
bash ./scripts/verify-virtualbox-iso.sh \
  ./dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso \
  --passes 20
```

Verifier sprawdza m.in. El Torito EFI, GPT ESP, FAT, `BOOTX64.EFI`, PE32+
AMD64 EFI Application, kernel, `install.pkg` oraz stabilny SHA-256.

## Diagnostyka

### `No bootable medium`

Sprawdź EFI64, canonical ISO, DVD-first boot i Secure Boot OFF.

### `no PCI AHCI controller`

Przenieś HDD na `SATA / Intel AHCI`. ISO może być całkowicie poprawne mimo tego
błędu.

### Stary ekran `INSTALLATION CONFIRMATION DID NOT MATCH INSTALL`

Uruchamiasz build sprzed poprawki confirmation flow. Wykonaj pełny rebuild i
upewnij się, że VM używa nowego canonical VirtualBox ISO z aktualną datą/hash.

### Po instalacji ponownie startuje Setup

Odłącz ISO albo ustaw `Disk -> Optical` po udanej instalacji.
