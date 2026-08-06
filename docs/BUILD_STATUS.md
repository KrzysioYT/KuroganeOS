# Build status

## Current stage

Utwardzanie platformy bazowej — samodzielny kernel preview x86-64 UEFI. Zakończono naprawy ścieżki startowej, diagnostyki, RAMFS/shella, konfiguracji buildów i testów obrazów. Walker VMM został połączony z aktywnymi tablicami UEFI oraz self-testem runtime. Nie rozpoczęto jeszcze właściwego etapu procesów ring 3 i trwałego systemu plików.

## Working

- Własny loader UEFI PE32+ i protokół startowy v2.
- Ładowanie oraz walidacja ELF64 PIE z relokacjami `R_X86_64_RELATIVE`.
- Konfiguracje `debug` i `release`, incremental fingerprint, build FAT32 oraz ISO.
- Serial, framebuffer, GDT/TSS/IST, IDT, PIC, PIT, klawiatura PS/2, RTC i enumeracja PCI.
- Stały heap kernela i ograniczony allocator jednego regionu fizycznego.
- Czteropoziomowy walker VMM oraz runtime map/write/translate/unmap na adoptowanym `CR3` UEFI.
- Hierarchiczny RAMFS z testowanymi limitami, copy/move/rename i rollbackiem błędów.
- Shell z CWD, ścieżkami względnymi, historią i poleceniami diagnostycznymi.
- Scheduler callbacków, stos protokołów sieciowych z loopbackiem i cztery widoki framebufferowe.
- Testy hostowe, scenariusze QEMU, bezpieczny smoke test VirtualBox i agregator `scripts/verify.ps1`.

## Partially working

- VMM: działa z aktywnym `CR3`, ale zachowuje identity map firmware, nie buduje własnej mapy kernela i nie egzekwuje docelowych ochron sekcji.
- PMM: wybiera tylko jeden region i ma statyczny limit bitmapy.
- Scheduler: callbacki kooperacyjne, bez kontekstów CPU, preempcji i procesów.
- Sieć: protokoły i loopback bez sterownika NIC oraz ruchu zewnętrznego.
- GUI: pełnoekranowe widoki z klawiaturą, bez myszy, powierzchni i window managera.
- SDK/ABI: nagłówki oraz build compile-only, bez transportu, linkowania i runtime.
- Restart/poweroff: porty sprzętowe/emulatorowe bez ACPI, więc nie są przenośne.
- Safe mode: ogranicza PCI, sieć i aplikacje, ale nie jest środowiskiem naprawy dysku.

## Broken

- Userspace/ring 3, syscalle, procesy, wątki i izolacja: niezaimplementowane.
- VFS, sterownik dysku, montowanie FAT32 i trwałość: niezaimplementowane.
- Mysz, kompozytor, window manager, osobny Terminal/Settings: niezaimplementowane.
- Sterownik NIC, ACPI, SMP i kwalifikacja realnego sprzętu: niezaimplementowane.

Powyższe pozycje nie są ukrytymi „gotowymi, lecz niedziałającymi” modułami. Są poza aktualnym zakresem implementacji i dlatego nie mogą być zaliczone jako funkcje wydania.

## Last test result

Stan na 2026-08-06:

- Bieżące `scripts/test.sh`: **PASS** — memory, virtual-memory (6 grup), RAMFS, scheduler, network, profiler, SDK ABI/test i generator projektów.
- Test VMM z ASan/UBSan oraz freestanding compile z rygorystycznymi ostrzeżeniami: **PASS** podczas implementacji modułu.
- Buildy debug/release oraz QEMU staged/disk/ISO: **PASS** w macierzy audytowej sprzed ostatnich równoległych zmian źródeł.
- Automatyczny VirtualBox EFI/SATA/ISO/COM1 headless: **PASS** dla ostatniego zbudowanego ISO; prompt osiągnięty, tymczasowa VM usunięta, plik ISO niezmieniony.
- Pełne `scripts/verify.ps1` po scaleniu wszystkich bieżących zmian: **DO PONOWNEGO URUCHOMIENIA**. Dopiero ten wynik potwierdzi finalne artefakty debug/release i wszystkie scenariusze QEMU razem.

## Next actions

- Uruchomić pełną macierz `scripts/verify.ps1`, opcjonalnie także VirtualBox, po zakończeniu wszystkich zmian.
- Zastąpić mapę firmware własną mapą kernela/HHDM, ochroną W^X/NX oraz docelową synchronizacją TLB.
- Dodać przełączanie kontekstu, proces ring 3, syscalle i izolację zasobów.
- Zaprojektować VFS, warstwę blokową i trwały filesystem przed rozbudową aplikacji.
- Dodać mysz i model powierzchni/kompozycji, a następnie rzeczywisty sterownik NIC i ACPI.
- Rozszerzyć regresję o długie działanie, presję pamięci i fizyczny sprzęt.

Szczegóły stanu: [PROJECT_AUDIT.md](PROJECT_AUDIT.md), [TESTING.md](TESTING.md) i [CURRENT_LIMITATIONS.md](CURRENT_LIMITATIONS.md).
