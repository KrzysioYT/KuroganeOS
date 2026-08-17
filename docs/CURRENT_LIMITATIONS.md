# Aktualne ograniczenia — KuroganeOS 3.3.3-dev

KuroganeOS 3.3.3-dev jest **DEV BETA**, a nie stabilnym systemem codziennego
użytku. Ten dokument mówi wprost co działa, co jest eksperymentalne i czego nie
należy jeszcze oczekiwać.

Jeżeli pierwszy raz uruchamiasz system, zacznij od [`START_HERE.md`](START_HERE.md).

## Co już istnieje

- własny UEFI x86-64 bootloader i boot protocol v3;
- VMM, GDT/TSS/IST, IDT i obsługa wyjątków;
- Ring 3, procesy ELF64, PID/TID, spawn/wait/exit i preempcja;
- `/system/init` jako PID 1;
- AHCI, GPT, writable FAT32/VFS i persistent root;
- publiczny Ring-3 filesystem ABI: read/write/append/seek, stat/readdir,
  create/unlink/rename, mkdir/rmdir i sync;
- Try/Install media i read-only live package root;
- instalator GPT/FAT32 z językiem, lokalnym profilem i opcjonalnym hasłem DEV;
- PS/2 keyboard/mouse, PCI, ACPI MADT/APIC discovery;
- E1000 `8086:100E` z Ethernet/ARP/IPv4/ICMP/UDP/DHCP/DNS i podstawowym TCP probe;
- loopback fallback, gdy DHCP/fizyczny interfejs nie może się skonfigurować;
- Intel ICH AC'97 `8086:2415` jako kernelowy PCM output backend;
- bounded Ring-3 AC'97 playback: 48 kHz S16LE stereo, max 1024 frames,
  per-PID ownership, poll/stop i kernel-owned DMA copy;
- WindowManager, Red Flux Desktop, Dock i aplikacje GUI Ring 3;
- software backbuffer i damage-style GOP scanout;
- SDK oraz build na Windows/WSL, macOS i Linux x86-64;
- ISO z El Torito EFI + GPT ESP i obowiązkowym 20-pass verifierem;
- pre-merge PR qualification oraz helper realnego VirtualBox smoke na hostach x86-64.

## Pamięć i wykonanie

- brak SMP i wykorzystania wielu CPU przez kernel;
- brak demand paging, copy-on-write, swap i pełnego mmap files;
- część struktur kernela nadal wymaga dalszego utwardzenia pod długotrwałą
  preempcję i przyszłe SMP;
- publiczne ABI jest eksperymentalne i może zmieniać się między DEV BETA.

## Userspace i shell

- filesystem ABI nie ma jeszcze file-backed mmap ani process-local
  cwd/chdir/getcwd;
- Try/live-package root pozostaje celowo read-only i odrzuca mutacje;
- brak links, pełnego modelu users/groups/ACL i Unix-like permissions;
- brak pipes, redirection, glob, zmiennych środowiskowych i języka skryptowego;
- background jobs są uproszczone względem pełnego Unix-like job control.

## Desktop

- Red Flux nadal renderuje programowo; nie jest jeszcze kompozytorem GPU;
- font/rendering, HiDPI, Unicode i animacje wymagają dalszej pracy;
- brak pełnego clipboard i multi-monitor;
- część compatibility `ku_ui_frame` nadal istnieje;
- brak natywnego per-window accelerated surface API.

## Storage / instalacja

- główny persistent filesystem pozostaje FAT32;
- brak pełnego recovery environment i transakcyjnych aktualizacji;
- NVMe nie jest jeszcze równorzędnym, szeroko zweryfikowanym backendem;
- instalatora nie należy kierować na dysk z ważnymi danymi;
- `FNV1A64-DEV` jest tymczasowym verifierem hasła, nie bezpiecznym KDF.

## VirtualBox

Referencyjny profil 3.3.x to x86-64 UEFI, SATA/AHCI, E1000 82540EM i AC'97.

Builder potrafi dowieść struktury nośnika i repo ma realny smoke boot helper, ale
nie istnieje matematyczna "100% gwarancja" dla każdej wersji VirtualBox, hosta i
ustawień VM. Release qualification wymaga zarówno automatycznych testów, jak i
realnego smoke na x86-64 VirtualBox.

Na Apple Silicon x86-64 KuroganeOS należy uruchamiać przez QEMU/TCG.

## Sieć

Kernel posiada rzeczywisty stos i referencyjny driver E1000 dla VirtualBox NAT.

**Userspace nie ma jeszcze stabilnego socket API.** Obecne kernelowe DNS/ping
helpers mają synchroniczny model pollingu. Zamiast utrwalać je jako długie
blocking syscalls, publiczne API zostanie zaprojektowane jako async handle/event
service.

## Audio

3.3.3 ma publiczny bounded Ring-3 playback nad Intel ICH AC'97:

```text
S16LE / stereo / 48 kHz / max 1024 frames / DMA32
```

To nadal nie jest finalny wielostrumieniowy serwis audio. Brakuje per-process
stream handles i mixera wielu aplikacji, ciągłego buffer scheduling z pełnym
underrun recovery, konwersji formatów/resamplingu, capture/microphone oraz Intel
HDA. Referencyjny hardware runtime smoke nadal wymaga realnego VirtualBox hosta
z działającym wyjściem audio.

## Grafika / DirectX

**KuroganeOS 3.3.3-dev nie obsługuje jeszcze pełnego DirectX/Direct3D 9/10/11/12.**

Obecnie dostępny jest software framebuffer/UI stack. Pełna zgodność D3D wymaga
native graphics runtime, zasobów, shaderów, command submission i backendu GPU.
Projekt ma architekturę/plan kompatybilności, ale nie udaje feature level, którego
realnie nie implementuje.

Zobacz [`GRAPHICS_COMPATIBILITY.md`](GRAPHICS_COMPATIBILITY.md).

## Hardware

- AHCI/SATA, PS/2, E1000 i AC'97 mają konkretne wspierane modele;
- real hardware UEFI ma mniejsze pokrycie niż VM;
- USB/xHCI nadal wymaga dalszej stabilizacji;
- brak produkcyjnego GPU acceleration, NVMe/audio-HDA support i szerokiej
  kwalifikacji ACPI/SMP;
- Guest Additions VirtualBox nie są portowane do KuroganeOS.

## Reliability

Każda rewizja systemu musi zostać ponownie zbudowana i uruchomiona. Sam fakt, że
kod się kompiluje albo że plik ISO istnieje, nie jest wystarczającym dowodem
runtime PASS.

Dla 3.3.x kwalifikacja ISO składa się z:

1. build od zera;
2. 20-pass El Torito/FAT/GPT/PE verifier;
3. niezależny UEFI optical smoke przez OVMF/QEMU;
4. realny VirtualBox smoke na x86-64 hoście;
5. test `ISO -> Install -> target HDD -> reboot -> Login`.
