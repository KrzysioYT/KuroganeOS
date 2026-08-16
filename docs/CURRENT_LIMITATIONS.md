# Aktualne ograniczenia — KuroganeOS 2.2

KuroganeOS 2.2 jest **Installable System + Desktop Developer Preview**, a nie
stabilnym systemem codziennego użytku. Ten dokument opisuje aktualny kod; stare
wersje twierdzące, że wszystko działa w Ring 0, są nieaktualne.

## Co już istnieje

- UEFI x86-64 bootloader i boot protocol v3;
- VMM, GDT/TSS/IST, IDT i obsługa wyjątków;
- Ring 3, procesy ELF64, PID/TID, spawn/wait/exit i preempcja;
- `/system/init` jako PID 1;
- AHCI, GPT, writable FAT32/VFS i persistent root;
- instalator oraz boot z zainstalowanego dysku;
- PS/2 keyboard/mouse, PCI, ACPI MADT/APIC discovery;
- E1000/networking w zakresie zaimplementowanym przez bieżący kernel;
- WindowManager i GUI Ring 3;
- SDK oraz development na Windows/WSL i macOS;
- Flux Console 2.2 i Kurogane Flux Desktop Developer Preview.

## Pamięć i wykonanie

- brak SMP i uruchamiania wielu CPU;
- brak demand paging, copy-on-write, swap i mmap files;
- część struktur kernela nadal wymaga dalszego utwardzenia pod długotrwałą
  preempcję i przyszłe SMP;
- publiczne ABI jest eksperymentalne i może zmieniać się między preview.

## Userspace i shell

- `open` w syscall ABI jest read-only;
- brak userspace `stat/readdir` oraz writable VFS API;
- dlatego `ls/stat/touch/mkdir/write/cp/mv/rm` nie są jeszcze pełnymi komendami
  Flux Console mimo że odpowiedniki developerskie istnieją w kernel shellu;
- brak pipes, redirection, glob, zmiennych środowiskowych i języka skryptowego;
- brak pełnego modelu users/groups/ACL;
- background jobs w Flux Console są celowo małą tabelą, nie pełnym job control.

## Desktop

- Kurogane Flux jest framebufferowym Developer Preview, nie kompozytorem GPU;
- WindowManager ma focus, z-order, drag, minimize/maximize/restore/close, ale
  resize i zaawansowane layouty nadal są ograniczone;
- część legacy widoków Ring 0 pozostaje w repo do diagnostyki;
- font/rendering, skalowanie i animacje są jeszcze proste;
- brak audio i pełnego desktop service layer.

## Storage / instalacja

- główny persistent filesystem pozostaje FAT32;
- brak pełnego recovery environment i transakcyjnych aktualizacji;
- NVMe nie jest jeszcze równorzędnym, szeroko zweryfikowanym backendem;
- instalatora nie należy kierować na dysk z ważnymi danymi.

## Hardware

- QEMU/EDK2 jest głównym środowiskiem kwalifikacji;
- real hardware UEFI ma mniejsze pokrycie;
- USB/xHCI, audio, GPU acceleration i szerszy ACPI nadal wymagają pracy;
- Apple Silicon jest hostem developerskim: KuroganeOS pozostaje x86-64 i jest
  tam emulowany przez QEMU TCG.

## Sieć

Kernel posiada rozwinięty stos i testy E1000/IPv4/DHCP/DNS w obsługiwanych
scenariuszach QEMU, ale userspace nie ma jeszcze socket API. Komendy sieciowe
Flux Console czekają na jawne capability/socket syscalls.

## Reliability

Commitowane logi 2.1 są dowodem wcześniejszych testów install/persistence, ale
każda nowa wersja powinna być ponownie budowana i bootowana. Sam commit 2.2 nie
jest dowodem fresh runtime PASS w QEMU/VirtualBox/macOS.
