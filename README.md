# KuroganeOS 3.3.3-dev — DEV BETA

KuroganeOS to eksperymentalny, 64-bitowy system operacyjny rozwijany od zera dla **x86-64 + UEFI**. Nie jest dystrybucją Linuxa i nie używa kernela Linux.

> [!IMPORTANT]
> Pierwszy raz tutaj? Zacznij od **[`docs/START_HERE.md`](docs/START_HERE.md)**. Pełna mapa dokumentacji znajduje się w **[`docs/README.md`](docs/README.md)**.

`3.3.3-dev` jest wydaniem **DEV BETA**. Używaj go przede wszystkim w QEMU albo Oracle VirtualBox i na pustych dyskach testowych.

## Szybki start

### Oracle VirtualBox — host Intel/AMD x86-64

Użyj canonical ISO:

```text
dist/KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso
```

Referencyjna konfiguracja:

```text
Firmware:       EFI64 / UEFI
Secure Boot:    OFF
I/O APIC:       ON
RAM:            2048 MiB
CPU:            1-2
Graphics:       VMSVGA / 128 MiB / 3D OFF
System disk:    SATA / Intel AHCI
Boot medium:    ISO jako IDE/PIIX4 DVD
Boot order:     DVD -> Hard Disk
Network:        NAT
NIC:            PCnet-FAST III (Am79C973)
Audio:          Intel AC'97
Keyboard/Mouse: PS/2
```

Pełna instrukcja: [`docs/VIRTUALBOX.md`](docs/VIRTUALBOX.md).

Windows helper:

```powershell
.\scripts\create-virtualbox-vm.ps1 `
    -Iso ".\dist\KuroganeOS-3.3.3-dev-virtualbox-x86_64.iso" `
    -Name "KuroganeOS-3.3.3-VB"
```

### QEMU

Canonical QEMU image:

```text
dist/KuroganeOS-3.3.3-dev-qemu-x86_64.img
```

Na Macach Apple Silicon KuroganeOS pozostaje gościem x86-64, dlatego do developmentu używaj QEMU/TCG.

## Try / Install

Red Flux Setup udostępnia dwa główne tryby:

```text
UEFI
 -> BOOTX64.EFI
 -> KuroganeOS kernel
 -> Red Flux Setup
      |-- TRY KUROGANEOS
      |    -> read-only live root
      |    -> Login
      |    -> Red Flux Desktop
      |
      `-- INSTALL KUROGANEOS
           -> język / konto
           -> wybór dysku SATA/AHCI
           -> potwierdzenie INSTALL
           -> GPT + ESP + Kurogane Root
           -> verification
```

Instalator jest destrukcyjny dla wybranego targetu. Używaj pustego VDI/obrazu/dysku testowego.

Pełna instrukcja: [`docs/INSTALLATION.md`](docs/INSTALLATION.md).

## Budowanie

### Windows 11 + WSL

Windows wymaga dodatkowego toolchainu opisanego w dokumentacji builda.

```powershell
.\scripts\build-media.ps1 -Configuration release -Rebuild
```

### macOS

```bash
bash ./scripts/setup-macos.sh --install
bash ./scripts/build-media-macos.sh --configuration release --rebuild
```

### Linux x86-64

```bash
bash ./scripts/setup-linux.sh --install
bash ./scripts/build-media-linux.sh --configuration release --rebuild
```

Szczegóły: [`docs/BUILDING.md`](docs/BUILDING.md).

## Aktualny stan systemu

KuroganeOS ma już m.in.:

- własny UEFI bootloader i kernel x86-64;
- VMM, GDT/TSS/IST, IDT;
- Ring 3, ELF64, PID/TID, spawn/wait/exit i preempcję;
- `/system/init` jako PID 1;
- AHCI, GPT, FAT32/VFS i persistent root;
- WindowManager oraz Red Flux Desktop;
- PS/2, PCI, ACPI/APIC discovery;
- E1000, PCnet i VirtIO-net dla środowisk VM;
- Ethernet/ARP/IPv4/ICMP/UDP/DHCP/DNS;
- rozwijany klient TCP;
- Mbed TLS 3.6.7, TLS 1.2/X.509, SNI i trust store;
- Intel ICH AC'97;
- SDK i aplikacje Ring-3.

Aktualny, techniczny status: [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md).  
Aktywne TODO: [`docs/ROADMAP.md`](docs/ROADMAP.md).  
Ograniczenia: [`docs/CURRENT_LIMITATIONS.md`](docs/CURRENT_LIMITATIONS.md).

## Sieć i HTTPS

Referencyjny VirtualBox networking:

```text
NAT + PCnet-FAST III (Am79C973)
```

QEMU kwalifikuje E1000, PCnet i VirtIO-net przez rzeczywisty user-NAT runtime.

KuroganeOS ma już ścieżkę:

```text
Ethernet
 -> ARP
 -> IPv4
 -> DHCP / DNS
 -> TCP
 -> HTTP
 -> TLS 1.2 / X.509
 -> HTTPS
```

HTTPS jest **zaimplementowane częściowo, ale nie jest jeszcze release-qualified end-to-end**. Bieżący blocker na `main` znajduje się w ścieżce TCP/BIO podczas Mbed TLS handshake i może kończyć się `net::Status::InterfaceError`.

Nie oznacza to braku całego TLS ani braku Internetu. Jeżeli DHCP, gateway i DNS są PASS, TLS/TCP należy diagnozować jako osobną warstwę.

Szczegóły: [`docs/NETWORKING.md`](docs/NETWORKING.md).

## VirtualBox qualification

Aktualny flow potrafi przejść UEFI boot, instalację na SATA/AHCI VDI, reboot z zainstalowanego dysku, persistent root, PID 1 oraz DHCP/gateway/DNS przez PCnet/NAT.

Pełny release-smoke pozostaje jednak otwarty z aktywnym blockerem:

```text
[TEST] fat32_persistence: FAIL
```

Nie opisujemy więc całej ścieżki jako końcowego PASS, dopóki runtime gate nie będzie zielony.

## Red Flux Desktop

Desktop zawiera m.in.:

- Try/Install Setup i Login;
- Red Flux Home;
- Dock i skróty aplikacji;
- Performance;
- Kurogane Web;
- Terminal, Files, System Monitor, Settings i About;
- focus/z-order, drag, resize, minimize/maximize/restore/close;
- software backbuffer i damage-style GOP scanout.

GPU acceleration i pełny Direct3D/DirectX 9/10/11/12 nie są jeszcze gotowe. Zobacz [`docs/GRAPHICS_COMPATIBILITY.md`](docs/GRAPHICS_COMPATIBILITY.md).

## Development

Aplikacje KuroganeOS są ELF64 x86-64 Ring-3, freestanding/static i używają KuroganeOS syscall ABI.

Start dla programisty:

- [`docs/DEVELOPERS/README.md`](docs/DEVELOPERS/README.md)
- [`docs/DEVELOPERS/APP_DEVELOPMENT.md`](docs/DEVELOPERS/APP_DEVELOPMENT.md)
- [`docs/DEVELOPERS/GUI_APPLICATIONS.md`](docs/DEVELOPERS/GUI_APPLICATIONS.md)
- [`docs/DEVELOPERS/API_REFERENCE.md`](docs/DEVELOPERS/API_REFERENCE.md)
- [`docs/DEVELOPERS/KERNEL_CONTRIBUTION.md`](docs/DEVELOPERS/KERNEL_CONTRIBUTION.md)

## Dokumentacja

Kanoniczny indeks: **[`docs/README.md`](docs/README.md)**.

Najważniejsze źródła prawdy:

- [`docs/START_HERE.md`](docs/START_HERE.md) — pierwszy start;
- [`docs/VIRTUALBOX.md`](docs/VIRTUALBOX.md) — Oracle VirtualBox;
- [`docs/INSTALLATION.md`](docs/INSTALLATION.md) — instalacja;
- [`docs/NETWORKING.md`](docs/NETWORKING.md) — sieć/TCP/TLS;
- [`docs/BUILD_STATUS.md`](docs/BUILD_STATUS.md) — bieżący snapshot;
- [`docs/CURRENT_LIMITATIONS.md`](docs/CURRENT_LIMITATIONS.md) — ograniczenia;
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — aktywny plan i status implementacji.

Pliki `docs/releases/*`, datowane audyty oraz legacy compatibility paths są historyczne i nie powinny nadpisywać aktualnego stanu `main`.

## Licencja

Aktualne rewizje KuroganeOS są udostępniane na warunkach **KuroganeOS Source-Available License 2.0 (KSAL-2.0)**.

- [`LICENSE`](LICENSE)
- [`docs/LICENSING.md`](docs/LICENSING.md)
- [`CLA.md`](CLA.md)
- [`CONTRIBUTING.md`](CONTRIBUTING.md)
- [`TRADEMARKS.md`](TRADEMARKS.md)
- [`LICENSE-MIT-LEGACY`](LICENSE-MIT-LEGACY) — historyczne rewizje MIT
