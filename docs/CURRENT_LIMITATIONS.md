# Aktualne ograniczenia — KuroganeOS 3.3.3-dev

KuroganeOS `3.3.3-dev` jest **DEV BETA**, nie stabilnym systemem codziennego
użytku. Bieżąca gałąź rozwija Forged Steel/KuroganeOS 5, ale nie jest jeszcze
wydaniem 5.0.0.

## Co już istnieje

- własny UEFI x86-64 bootloader i boot protocol;
- VMM, GDT/TSS/IST, IDT i exception handling;
- ELF64 Ring-3, PID/TID, spawn/wait/exit, timer preemption;
- `/system/init` jako stabilny PID 1;
- AHCI, GPT, FAT32/VFS i persistent `Kurogane Root`;
- publiczny Ring-3 filesystem ABI z `stat/readdir`, cwd i mutacjami;
- Try/Install media i lokalny profil użytkownika;
- PS/2 keyboard/mouse, PCI, ACPI MADT/APIC discovery;
- E1000 z Ethernet/ARP/IPv4/ICMP/UDP/DHCP/DNS/TCP;
- HTTP oraz HTTPS/TLS używane przez Kurogane Web;
- Intel ICH AC'97 i bounded Ring-3 playback;
- WindowManager i Forged Steel desktop;
- native UI ABI v2 z widget hierarchy/icon IDs/cursor hints;
- Blade Launcher, Kurosh, Vault, Anvil, Forge Control, Pulse, Web,
  Performance, System Monitor i About;
- software backbuffer + GOP scanout;
- SDK/build tooling dla Windows+WSL, macOS i Linux;
- QEMU/VirtualBox media validation.

## Pamięć / CPU

- kernel nie używa jeszcze SMP do równoległej pracy wielu CPU;
- brak demand paging, copy-on-write, swap i pełnego file-backed mmap;
- część struktur wymaga dalszego utwardzenia przed SMP;
- publiczne ABI nadal może zmieniać się pomiędzy DEV BETA.

## Userspace

- brak pełnego POSIX;
- brak links, pełnego users/groups/ACL/Unix permissions;
- shell nie ma jeszcze kompletnego pipes/redirection/glob/environment/script
  language;
- background jobs są uproszczone;
- publiczne socket API nie jest jeszcze finalnym async handle/event service.

## Forged Steel Desktop

Największe aktywne ograniczenia GUI:

- software compositor nad UEFI GOP, bez produkcyjnej akceleracji GPU;
- wydajność pod QEMU TCG może być niska, szczególnie przy wysokiej rozdzielczości;
- trwa optymalizacja dirty/redraw/scanout i input latency;
- bitmapowy font subsystem zamiast pełnego TTF/OpenType;
- layout widgetów jest nadal głównie flow-based;
- brak pełnego Unicode/IME, clipboard i accessibility;
- brak multi-monitor;
- część internal window roles nadal zależy od stabilnych technicznych tytułów;
- legacy `ku_ui_frame` pozostaje dla kompatybilności obok native ABI v2.

Do testów responsywności na Windows preferuj WHPX przez
`scripts/run-qemu-fast.ps1`. TCG jest poprawnym funkcjonalnie fallbackiem, ale
nie jest miarodajnym benchmarkiem FPS.

## Grafika / Direct3D

KuroganeOS **nie ma jeszcze pełnej zgodności Direct3D 9/10/11/12**.

Istnieją warstwy API/testy compatibility i rozwijany software graphics stack,
ale pełna zgodność wymaga m.in. zasobów GPU, shaderów, command submission,
synchronizacji, presentation i realnego backendu hardware/software. Nie należy
interpretować obecności nazw D3D jako pełnego feature level.

Zobacz [GRAPHICS_COMPATIBILITY.md](GRAPHICS_COMPATIBILITY.md).

## Storage / instalacja

- główny persistent filesystem nadal opiera się na FAT32;
- instalator ma twardy FAT 8.3 contract dla package paths;
- dlatego Anvil config używa `/etc/anvil.cfg`, nie `/etc/anvil.repo`;
- brak pełnego recovery environment;
- brak ogólnego transakcyjnego system update/rollback;
- NVMe nie jest jeszcze równorzędnym referencyjnym backendem;
- DEV credential hash nie jest produkcyjnym password KDF;
- instalatora nie kieruj na dysk z ważnymi danymi.

## Sieć / Web

Kernel ma własny stos i referencyjny E1000 dla QEMU/VirtualBox NAT.
Kurogane Web ma HTTP/HTTPS, trust store, redirecty, historię i prosty
HTML/CSS rendering.

Ograniczenia Web:

- to nie Chromium;
- brak JavaScript engine;
- brak pełnego DOM/CSS layout engine;
- brak WebGL/WebGPU;
- API sieciowe aplikacji nadal jest węższe niż POSIX/BSD sockets;
- obsługa współczesnego webu jest celowo ograniczona.

## Audio

AC'97 playback działa w ograniczonym modelu:

```text
S16LE / stereo / 48 kHz / bounded buffer / DMA32
```

Brakuje m.in. produkcyjnego wielostrumieniowego mixera, resamplingu, capture,
pełnego underrun recovery i Intel HDA.

## Hardware

Referencyjne modele to głównie:

```text
AHCI/SATA
PS/2
E1000 82540EM
Intel AC'97
UEFI GOP
```

Real hardware ma mniejsze pokrycie niż VM. USB/xHCI, ACPI diversity, NVMe,
HDA i GPU wymagają dalszej kwalifikacji. Guest Additions VirtualBox nie są
portowane.

## QEMU / VirtualBox

Pełny userspace wymaga Foundation GPT (`build/images/KuroganeOS-base.img`) albo
working image. Legacy `kurogane.img` jest tylko FAT/EFI artifactem i nie ma
`Kurogane Root`.

VirtualBox nie daje gwarancji działania na każdej kombinacji hosta/wersji.
Release wymaga realnej kwalifikacji, nie tylko poprawnej struktury ISO.

Na Apple Silicon x86-64 KuroganeOS działa przez QEMU TCG.

## Reliability

Build PASS nie oznacza runtime PASS. Dla bieżącej gałęzi wymagamy kolejno:

1. host tests;
2. FAT/GPT validation;
3. Foundation QEMU boot;
4. PID1 + login + Blade session;
5. storage/network/input markers;
6. test profile i AHCI scratch;
7. release build/ISO;
8. QEMU ISO qualification;
9. dla release media — odpowiedniej kwalifikacji VirtualBox.

Pełny workflow opisuje [TESTING.md](TESTING.md).
