# Desktop roadmap — stan po KuroganeOS 2.2

Roadmapa została zaktualizowana po wdrożeniu fundamentów, które stare dokumenty
nadal błędnie opisywały jako przyszłość.

## Zrealizowane fundamenty

- [x] GDT/TSS/IST, IDT i diagnostyka wyjątków;
- [x] czteropoziomowe page tables i VMM;
- [x] Ring 3 oraz prywatne przestrzenie adresowe;
- [x] syscall ABI `int 0x80`;
- [x] procesy ELF64, PID, spawn/wait/exit;
- [x] osobne stosy i preempcja PIT;
- [x] `/system/init` jako PID 1;
- [x] BlockDevice/AHCI, GPT, writable FAT32/VFS i persistent root;
- [x] instalator UEFI i boot systemu z HDD po odłączeniu ISO;
- [x] PS/2 keyboard/mouse oraz wspólna kolejka input;
- [x] WindowManager: focus, z-order, drag, minimize/maximize/restore/close;
- [x] pierwsze GUI Ring 3 i publiczne libui;
- [x] SDK oraz budowanie własnych aplikacji;
- [x] Windows/WSL oraz natywny macOS development workflow;
- [x] Kurogane Flux Desktop Developer Preview 2.2;
- [x] rozbudowana Ring-3 Flux Console z uruchamianiem ELF i job tracking.

## 2.2.x — domknięcie Developer Preview

1. Dodać read-only userspace `stat/readdir` i przenieść prawdziwe `ls/stat`.
2. Dodać kontrolowane writable VFS capabilities dla userspace zamiast dostępu
   do kernel shella.
3. Dodać system-info capabilities: uptime, memory/process/device snapshot.
4. Dodać network query/socket ABI dla aplikacji użytkownika.
5. Dodać poprawne resize okien i damage regions.
6. Usunąć pozostałe zależności aplikacji desktopowych od legacy Ring-0 views.
7. Rozszerzyć testy desktopu o scenariusze wielu aplikacji i długie sesje.

## 2.3 — Desktop Services

- userspace session service;
- application registry/launcher zamiast list hard-coded;
- settings service i trwałe preferencje;
- notifications/event broker;
- podstawowe identity/permissions;
- mount/service APIs i bezpieczne shutdown/reboot capabilities.

## 2.4 — Compositor

- niezależne surfaces i backbuffers;
- damage tracking;
- resize;
- clipping;
- lepsze fonty i skalowanie;
- animacje tylko tam, gdzie nie naruszają responsywności;
- bez kopiowania layoutu Windows/macOS/Linux desktop environments.

## Dalsze etapy

NVMe, USB/xHCI, audio, SMP, szerszy ACPI, package/update transactions, recovery
i real-hardware qualification pozostają niezależnymi torami rozwoju. Nie należy
blokować każdej iteracji UX oczekiwaniem na komplet tych subsystemów, ale żadna
warstwa wizualna nie może maskować braków bezpieczeństwa lub trwałości.
