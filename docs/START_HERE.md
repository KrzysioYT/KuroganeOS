# KuroganeOS — START HERE

KuroganeOS **3.3.3-dev / DEV BETA** jest eksperymentalnym systemem x86-64 rozwijanym od zera. Najbezpieczniej uruchamiać go w maszynie wirtualnej i na pustych dyskach testowych.

Pełna mapa dokumentacji: [`README.md`](README.md).

## Wybierz właściwy artefakt

```text
Oracle VirtualBox:
  dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso

QEMU:
  dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
```

Nie używaj QEMU `.img` jako napędu DVD VirtualBox i nie zakładaj, że stare nazwy artefaktów są równoważne z bieżącym buildem.

## Referencyjny Oracle VirtualBox

```text
Firmware:       EFI64 / UEFI
Secure Boot:    OFF
I/O APIC:       ON
RAM:            2048 MiB
CPU:            1-2
Graphics:       VMSVGA / 128 MiB / 3D OFF
HDD:            pusty VDI >= 2 GiB
HDD controller: SATA / Intel AHCI
DVD controller: IDE / PIIX4
DVD:            KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
Boot order:     DVD -> Disk
Network:        NAT
NIC:            PCnet-FAST III (Am79C973)
Audio:          Intel AC'97
Input:          PS/2
```

Najważniejsze:

```text
SATA / IntelAHCI -> KuroganeOS.vdi
IDE / PIIX4      -> KuroganeOS VirtualBox ISO
```

Canonical VirtualBox NIC to obecnie **PCnet-FAST III**. E1000 i VirtIO-net pozostają obsługiwanymi/testowymi profilami, ale nie są aktualnym domyślnym profilem Oracle VirtualBox.

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
7. Poczekaj na `[TEST] installer_complete: PASS`.
8. Wyłącz VM i odłącz ISO.
9. Uruchom z VDI.

Pełna instrukcja: [`INSTALLATION.md`](INSTALLATION.md).

> Pełny release-smoke instalacji nie jest jeszcze całkowicie zielony: aktywny roadmap raportuje `fat32_persistence: FAIL`. Samo `installer_complete: PASS` nie oznacza jeszcze pełnej kwalifikacji reboot/persistence.

## Windows — zbuduj media

```powershell
.\scripts\build-media.ps1 -Configuration release -Rebuild
```

## Windows — utwórz poprawną VM

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

Repair helper ustawia EFI64, DVD -> Disk, IntelAHCI i potrafi przenieść istniejący HDD na SATA 0:0 bez tworzenia nowego dysku.

## Internet w VirtualBox

```text
Attached to: NAT
Adapter Type: PCnet-FAST III (Am79C973)
Cable Connected: ON
```

Zdrowa bazowa ścieżka sieciowa powinna dojść co najmniej do:

```text
[TEST] dhcp_lease: PASS
[TEST] network_gateway_icmp: PASS
[TEST] dns_resolver: PASS
```

Jeżeli te testy przechodzą, a HTTPS nie działa, problem należy diagnozować w TCP/TLS, nie jako „brak internetu”.

## TLS / HTTPS — aktualny stan

KuroganeOS ma już Mbed TLS 3.6.7, TLS 1.2, X.509, SNI, trust store i walidację czasu. HTTPS jest podłączone do Ring-3/Kurogane Web, ale **nie jest jeszcze release-qualified end-to-end**.

Aktualny blocker na `main` znajduje się w ścieżce TCP/BIO podczas handshake: `tcp_client::send()` może doprowadzić do `net::Status::InterfaceError`, który BIO mapuje na błąd Mbed TLS.

Najważniejsze pliki:

```text
kernel/net/tls/client.cpp
kernel/net/tcp_client.cpp
kernel/net/network.cpp
kernel/net/service.cpp
```

### `x509_crt_parse ... D9D2`

To wcześniejszy problem parsowania algorytmu podpisu/OID (`SHA-384`). Bieżący profil wymaga `MBEDTLS_SHA384_C`. Jeżeli świeży build nadal zatrzymuje się dokładnie na `D9D2`, najpierw sprawdź, czy na pewno uruchamiasz nowo przebudowany canonical artifact, a nie starszy ISO/IMG.

Szczegóły: [`NETWORKING.md`](NETWORKING.md) i [`BUILD_STATUS.md`](BUILD_STATUS.md).

## Najczęstsze błędy

### `No bootable medium`

Sprawdź EFI64, Secure Boot OFF, canonical `.iso` i boot order DVD -> Disk.

### `[FATAL][INSTALL] no PCI AHCI controller`

ISO już wystartowało. Problemem jest storage. Przenieś VDI na SATA / Intel AHCI.

### `NIC OFFLINE`

Sprawdź NAT, Cable Connected oraz PCnet-FAST III. Jeżeli celowo testujesz E1000/VirtIO, traktuj to jako profil testowy, nie canonical VBox.

### HTTPS: `BIO send network status=...` / `InterfaceError`

Zapisz cały serial log od TCP connect do błędu. Potrzebne są co najmniej wartości:

```text
TCP state
SND.UNA
SND.NXT
RCV.NXT
peer window
BIO requested bytes
BIO accepted bytes
```

Nie obchodź problemu downgrade'em HTTPS -> HTTP i nie wyłączaj weryfikacji certyfikatów.

## Rozwój aplikacji i kernela

Aplikacje:

- [`DEVELOPERS/README.md`](DEVELOPERS/README.md)
- [`DEVELOPERS/APP_DEVELOPMENT.md`](DEVELOPERS/APP_DEVELOPMENT.md)
- [`DEVELOPERS/API_REFERENCE.md`](DEVELOPERS/API_REFERENCE.md)

Kernel/system:

- [`DEVELOPERS/KERNEL_CONTRIBUTION.md`](DEVELOPERS/KERNEL_CONTRIBUTION.md)
- [`ARCHITECTURE.md`](ARCHITECTURE.md)
- [`ROADMAP.md`](ROADMAP.md)

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
