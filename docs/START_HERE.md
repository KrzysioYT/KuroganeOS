# KuroganeOS — START HERE

KuroganeOS **3.3.3-dev DEV BETA** jest systemem x86-64 rozwijanym od zera.
Najbezpieczniej uruchamiać go w maszynie wirtualnej. Nie instaluj DEV BETA na
fizycznym dysku zawierającym ważne dane.

## Wybierz właściwy artefakt

Na Windows canonical media są rozdzielone według hypervisora:

```text
Oracle VirtualBox:
  dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso

QEMU:
  dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
```

Nie używaj `.img` jako VirtualBox DVD.

## Chcę uruchomić KuroganeOS w VirtualBox

Referencyjna konfiguracja:

```text
Firmware:       EFI64 / UEFI
Secure Boot:    OFF
I/O APIC:       ON
RAM:            2048 MiB
CPU:            1-2
HDD:            pusty VDI >= 2 GiB
HDD controller: SATA / Intel AHCI
DVD controller: IDE / PIIX4
DVD:            KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
Boot order:     DVD -> Disk
Network:        NAT
NIC:            Intel PRO/1000 MT Desktop (82540EM)
Audio:          Intel AC'97
Input:          PS/2
```

Najważniejsze:

```text
SATA / IntelAHCI -> KuroganeOS.vdi
IDE / PIIX4      -> KuroganeOS VirtualBox ISO
```

Jeżeli VDI jest na IDE, ISO może się uruchomić, ale instalator nie zobaczy
wymaganego PCI AHCI.

Szczegóły: [`VIRTUALBOX.md`](VIRTUALBOX.md).

## Chcę tylko zobaczyć system

1. Uruchom VirtualBox ISO albo QEMU IMG.
2. W Red Flux Setup wybierz `TRY KUROGANEOS`.
3. System uruchomi sesję live bez formatowania dysku.

Na Macach Apple Silicon używaj QEMU/TCG dla gościa x86-64.

## Chcę zainstalować w VirtualBox

1. Upewnij się, że VDI jest podpięty przez SATA / Intel AHCI.
2. Uruchom canonical VirtualBox ISO.
3. Wybierz `INSTALL KUROGANEOS`.
4. Wybierz język, username i tryb hasła.
5. Wybierz pusty wirtualny dysk.
6. Na ekranie destrukcyjnego potwierdzenia wpisz `INSTALL`.
7. Wielkość liter nie ma znaczenia — `install`, `Install` i `INSTALL` są
   akceptowane.
8. Błędny tekst można poprawić bez restartu instalatora.
9. `Esc` wraca do wyboru dysku i nie zapisuje GPT.
10. Poczekaj na `[TEST] installer_complete: PASS`.
11. Wyłącz VM, odłącz ISO i uruchom z HDD.

Pełna instrukcja: [`INSTALLATION.md`](INSTALLATION.md).

## Windows — zbuduj media

```powershell
.\scripts\build-media.ps1 -Configuration release -Rebuild
```

Windows build domyślnie uruchamia realny Oracle VirtualBox smoke dla canonical
ISO. Jeżeli świadomie budujesz na hoście bez VirtualBox:

```powershell
.\scripts\build-media.ps1 `
    -Configuration release `
    -Rebuild `
    -SkipVirtualBoxSmoke
```

Taki build nie jest runtime-qualified jako VirtualBox PASS.

## Windows — utwórz poprawną VM automatycznie

```powershell
.\scripts\create-virtualbox-vm.ps1 `
    -Iso ".\dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso" `
    -Name "KuroganeOS-3.3.3-VB"
```

## Windows — napraw istniejącą VM

VM musi być całkowicie wyłączona:

```powershell
.\scripts\repair-virtualbox-boot.ps1 `
    -Name "KuroganeOS" `
    -Iso ".\dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso"
```

Repair helper ustawia EFI64, DVD -> Disk, IntelAHCI i potrafi przenieść
wirtualny HDD z IDE na SATA 0:0 bez tworzenia nowego dysku.

## Najczęstsze błędy

### `No bootable medium`

Sprawdź EFI64, Secure Boot OFF, canonical `.iso` i boot order DVD -> Disk.

### `[FATAL][INSTALL] no PCI AHCI controller`

ISO już wystartowało. Przenieś VDI na SATA / Intel AHCI.

### `INSTALLATION CONFIRMATION DID NOT MATCH INSTALL`

To stary build instalatora. Bieżący flow nie zatrzymuje instalacji przy złym
wpisie i nie rozróżnia wielkości liter. Zbuduj nowe media i sprawdź hash/datę
ISO.

### TLS `x509_crt_parse ... D9D2`

Kod `-0x262E` oznacza brak obsługi algorytmu podpisu/OID podczas parsowania
trust store. Bieżący profil Mbed TLS 3.6.7 włącza `MBEDTLS_SHA384_C`, wymagane
przez dołączone rooty GTS. Jeżeli widzisz ten błąd po aktualizacji brancha,
upewnij się, że uruchamiasz świeżo przebudowany canonical QEMU IMG/VirtualBox ISO,
a nie starszy artefakt.

Szczegóły sieci: [`NETWORKING.md`](NETWORKING.md).

## Internet w VirtualBox

```text
Attached to: NAT
Adapter Type: Intel PRO/1000 MT Desktop (82540EM)
Cable Connected: ON
```

## Rozwój aplikacji i kernela

Aplikacje:

- [`DEVELOPERS/README.md`](DEVELOPERS/README.md)
- [`DEVELOPERS/APP_DEVELOPMENT.md`](DEVELOPERS/APP_DEVELOPMENT.md)
- [`DEVELOPERS/API_REFERENCE.md`](DEVELOPERS/API_REFERENCE.md)

Kernel/system:

- [`DEVELOPERS/KERNEL_CONTRIBUTION.md`](DEVELOPERS/KERNEL_CONTRIBUTION.md)
- [`ARCHITECTURE.md`](ARCHITECTURE.md)
- [`GUI.md`](GUI.md)

## Zgłaszanie błędu

Podaj co najmniej:

```text
KuroganeOS version / commit:
Host OS:
QEMU / VirtualBox version:
CPU architecture hosta:
Użyty plik IMG/ISO i jego SHA-256:
Try czy Install:
Konfiguracja storage/NIC:
Ostatnie linie serial log:
Screenshot:
```
