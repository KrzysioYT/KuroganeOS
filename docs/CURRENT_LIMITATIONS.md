# Aktualne ograniczenia — KuroganeOS 3.3.3-dev

KuroganeOS 3.3.3-dev jest **DEV BETA**, a nie stabilnym systemem codziennego użytku. Ten dokument opisuje ograniczenia bieżącej linii. Aktywny stan prac znajduje się w [`ROADMAP.md`](ROADMAP.md), a krótki snapshot kwalifikacji w [`BUILD_STATUS.md`](BUILD_STATUS.md).

Jeżeli pierwszy raz uruchamiasz system, zacznij od [`START_HERE.md`](START_HERE.md).

## Co już istnieje

- własny UEFI x86-64 bootloader i boot protocol v3;
- VMM, GDT/TSS/IST, IDT i obsługa wyjątków;
- Ring 3, ELF64, PID/TID, spawn/wait/exit i preempcja;
- `/system/init` jako PID 1;
- AHCI, GPT, writable FAT32/VFS i persistent root;
- publiczny Ring-3 filesystem ABI z podstawowymi operacjami plikowymi i katalogowymi;
- Try/Install media i read-only live package root;
- PS/2 keyboard/mouse, PCI, ACPI/APIC discovery;
- E1000, PCnet oraz VirtIO-net dla środowisk VM;
- Ethernet/ARP/IPv4/ICMP/UDP/DHCP/DNS oraz rozwijany klient TCP;
- Mbed TLS 3.6.7, TLS 1.2/X.509, SNI, trust store i HTTPS path;
- Intel ICH AC'97 jako kernelowy PCM backend;
- WindowManager, Red Flux Desktop, Dock i aplikacje GUI Ring 3;
- SDK oraz build na Windows/WSL, macOS i Linux x86-64.

## Pamięć i wykonanie

- brak SMP i rzeczywistego wykorzystania wielu CPU przez kernel;
- brak demand paging, copy-on-write, swap i pełnego file-backed mmap;
- część struktur kernela nadal wymaga dalszego utwardzenia pod długotrwałą preempcję i przyszłe SMP;
- publiczne ABI jest eksperymentalne i może zmieniać się między wydaniami DEV BETA.

## Userspace i shell

- brak pełnych pipes/redirection/environment/glob;
- job control jest uproszczony;
- brak pełnego modelu Unix-like users/groups/permissions/ACL;
- większe porty nadal blokuje zbyt mały libc/POSIX compatibility surface;
- publiczne userspace threads i pełny zestaw mutex/condvar/futex-like wait nie są jeszcze ukończone.

## Desktop i grafika

- Red Flux nadal renderuje głównie programowo;
- brak produkcyjnego GPU acceleration i docelowego per-window accelerated surface API;
- font/rendering, HiDPI, Unicode/text shaping, clipboard i multi-monitor wymagają dalszej pracy;
- część compatibility `ku_ui_frame` nadal istnieje;
- KuroganeOS nie implementuje pełnego Direct3D/DirectX 9/10/11/12.

## Storage / instalacja

- główny persistent filesystem pozostaje FAT32;
- brak recovery environment i transakcyjnych aktualizacji systemu;
- NVMe nie jest jeszcze równorzędnym, szeroko zweryfikowanym backendem;
- instalatora nie należy kierować na dysk z ważnymi danymi;
- `FNV1A64-DEV` jest tymczasowym verifierem hasła, nie bezpiecznym password KDF;
- pełny VirtualBox install -> detach ISO -> reboot smoke nadal ma aktywny blocker `fat32_persistence: FAIL`.

## VirtualBox

Canonical profil Oracle VirtualBox dla 3.3.3-dev to:

```text
x86-64 UEFI / EFI64
SATA / Intel AHCI
PCnet-FAST III (Am79C973) + NAT
Intel AC'97
```

E1000 pozostaje wspierany i kwalifikowany w QEMU, ale nie jest obecnie canonical VirtualBox NIC. VirtIO-net również istnieje i jest kwalifikowany pod QEMU; realny VirtualBox VirtIO smoke pozostaje osobnym zadaniem.

Builder i CI potrafią zweryfikować strukturę mediów oraz wiele ścieżek runtime, ale nie istnieje „100% gwarancja” dla każdej wersji VirtualBox/hosta/konfiguracji.

Na Apple Silicon x86-64 KuroganeOS należy uruchamiać przez QEMU/TCG.

## Sieć i HTTPS

Bazowa sieć VM ma realne runtime testy DHCP/gateway/DNS. Problem HTTPS nie powinien być automatycznie klasyfikowany jako „brak internetu”, jeżeli te etapy są zielone.

KuroganeOS ma już:

- klient TCP z SND.UNA/SND.NXT/RCV.NXT;
- bounded out-of-order buffering;
- retransmisję;
- Mbed TLS 3.6.7;
- X.509/trust store;
- RTC/time verification;
- Ring-3 HTTPS entry point.

Jednocześnie **HTTPS nie jest jeszcze release-qualified end-to-end**. Bieżący runtime blocker jest w ścieżce TCP/BIO send podczas Mbed TLS handshake i może kończyć się `net::Status::InterfaceError`.

Nie wolno maskować tego przez:

- downgrade HTTPS -> HTTP;
- wyłączenie certificate verification;
- akceptowanie wszystkich certyfikatów;
- traktowanie błędu TCP jako sukcesu TLS.

Publiczne async socket handles, readiness/event waits i async DNS nadal są planowane.

## Audio

3.3.3 ma bounded Ring-3 playback nad Intel ICH AC'97, ale nie ma jeszcze finalnego wielostrumieniowego serwisu audio, pełnego mixera, resamplingu, capture/microphone ani Intel HDA.

## Hardware

- AHCI/SATA, PS/2, E1000, PCnet, VirtIO-net i AC'97 mają konkretne wspierane profile;
- real hardware UEFI ma mniejsze pokrycie niż VM;
- xHCI/USB HID wymaga dalszej stabilizacji;
- brak szerokiej obsługi fizycznych NIC, NVMe, Intel HDA i produkcyjnego GPU acceleration;
- Guest Additions VirtualBox nie są portowane do KuroganeOS.

## Reliability i kwalifikacja

Sam fakt, że kod się kompiluje albo że powstał plik ISO, nie jest wystarczającym dowodem runtime PASS.

Dla istotnych zmian wymagamy odpowiedniej kombinacji:

1. clean build;
2. host regression tests;
3. media verification;
4. OVMF/QEMU runtime smoke;
5. właściwego VM/hardware smoke dla subsystemu;
6. aktualizacji `ROADMAP.md` i dokumentacji subsystemu.

Dla HTTPS końcowym kryterium jest realna ścieżka:

```text
DNS -> TCP -> TLS handshake -> X.509/hostname/time -> HTTPS request -> response
```
