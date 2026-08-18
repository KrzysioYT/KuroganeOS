# Sterowniki i obsługa platformy

Ten dokument opisuje bieżącą linię **KuroganeOS 3.3.x**. Dokładny stan
kwalifikacji VM należy sprawdzać razem z
[`compatibility/HARDWARE_MATRIX.md`](compatibility/HARDWARE_MATRIX.md) i
ostatnim workflow `KuroganeOS UEFI ISO qualification`.

## Zaimplementowane warstwy

| Moduł | Rzeczywisty zakres |
|---|---|
| UEFI GOP / framebuffer | liniowy framebuffer przekazany przez loader, programowe 2D/tekst i software compositor; brak sprzętowego 3D command submission |
| Serial | COM1/16550 i logowanie diagnostyczne |
| GDT/TSS/IST + IDT | x86-64 privilege/exception foundation i centralny dispatcher IRQ |
| PIC / PIT / RTC | legacy interrupt/timer/time compatibility path używany przez bieżące VM |
| PS/2 keyboard + mouse | podstawowa ścieżka wejścia dla emulatorów; USB HID nadal wymaga szerszej kwalifikacji |
| PCI | enumeracja konfiguracji, BAR-y, klasy/ID i bus mastering |
| Device/Driver Manager | rejestr urządzeń/sterowników, match/probe/attach i timeout budget |
| SATA / AHCI | bounded block I/O, IDENTIFY, read/write/flush i instalacyjny target dla VM |
| FAT32 / VFS | root/ESP i zapisy instalatora; szczegóły w dokumentacji filesystem/storage |
| Intel ICH AC'97 | PCM output 48 kHz S16LE stereo dla kompatybilnego emulowanego urządzenia |
| Intel E1000 | własny backend PCI/DMA dla 82540EM |
| AMD PCnet | backend zgodności dla starszych wirtualnych NIC |
| VirtIO-net PCI | transitional/modern PCI, capability discovery, split virtqueues i RX/TX DMA |
| xHCI / USB | kod backendu/protokołu istnieje, ale szeroka kompatybilność sprzętowa nadal jest etapem rozwoju |

Enumeracja PCI nie oznacza automatycznie obsługi dowolnego urządzenia. Każda
rodzina NIC, storage, audio lub GPU wymaga odpowiedniego backendu oraz runtime
kwalifikacji.

## Sieć

Aktualna kolejność wykrywania fizycznego backendu sieciowego w kernelu:

```text
VirtIO-net -> E1000 -> PCnet
```

Po wyborze backendu wszystkie korzystają ze wspólnego stosu:

```text
Ethernet -> ARP -> IPv4 -> ICMP / UDP / DHCP / DNS / TCP -> HTTP/TLS foundation
```

Szczegóły i ograniczenia: [`NETWORKING.md`](NETWORKING.md).

Dla VirtualBox E1000/82540EM pozostaje domyślnym profilem zgodności. VirtIO-net
jest właściwym kierunkiem parawirtualizowanym, ale bezwarunkowy status
VirtualBox wymaga realnego smoke na hoście x86-64, nie tylko testu QEMU.

## Storage i boot

Referencyjna instalacja używa SATA / Intel AHCI. Oficjalne ISO jest **UEFI-only**;
legacy BIOS nie jest obecnie obsługiwany. Instalator tworzy GPT z EFI System
Partition oraz kopiuje fallbackowy `EFI/BOOT/BOOTX64.EFI` na docelowy ESP.

Nie należy konfigurować NVMe jako jedynego dysku instalacyjnego do czasu
implementacji odpowiedniego backendu.

## Grafika

Kernel wykrywa PCI display adapter, ale aktywna powierzchnia obrazu pochodzi z
UEFI GOP. `redflux-display` potwierdza dostępny GOP scanout i software compositor.
To nie jest sterownik sprzętowej akceleracji GPU i nie oznacza kompletnego
Direct3D/DirectX runtime.

## Audio

Builtin driver manager rejestruje Intel ICH AC'97 (`8086:2415`). Implementacja
inicjalizuje własną ścieżkę PCM i DMA. Szersza obsługa Intel HDA oraz innych
kontrolerów pozostaje osobnym etapem.

## Nadal brakujące / niekwalifikowane rodziny

- NVMe i VirtIO-blk;
- szeroka obsługa realnych kart Ethernet (np. popularne rodziny Realtek i
  nowsze Intel) oraz Wi-Fi;
- pełna kompatybilność USB HID/xHCI na wielu chipsetach;
- Intel HDA i szerszy audio stack;
- sprzętowa akceleracja GPU/3D;
- pełne ACPI power-management i SMP jako produkcyjna platforma wielordzeniowa;
- runtime-loadable third-party driver model.

Aktualne ograniczenia: [`CURRENT_LIMITATIONS.md`](CURRENT_LIMITATIONS.md).
Pełny audit repo: [`AUDIT_2026-08-18.md`](AUDIT_2026-08-18.md).
