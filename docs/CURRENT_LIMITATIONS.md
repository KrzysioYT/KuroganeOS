# Aktualne ograniczenia

## Klasyfikacja wydania

KuroganeOS 1.0 jest obecnie **wersją demonstracyjną kernela (kernel preview)**. Potrafi samodzielnie wystartować przez UEFI, udostępnić framebufferowy shell, ulotny RAMFS i kilka widoków, ale nie spełnia jeszcze kryteriów stabilnego systemu codziennego użytku. Numer wersji w bannerze nie zmienia tej klasyfikacji.

## Ochrona i model wykonania

- Wszystko wykonuje się w ring 0 i jednej przestrzeni adresowej.
- Nie ma procesu `init`, programów użytkownika, syscalli, przełączania kontekstu ani izolacji pamięci.
- Scheduler wywołuje callbacki na stosie kernela; nie jest schedulerem procesów ani wątków.
- Brak preempcji, SMP, per-CPU state, spinlocków i uruchamiania dodatkowych procesorów.
- GDT ma przygotowane selektory użytkownika, ale kernel z nich nie korzysta do uruchamiania kodu.
- Walidacja obrazu zabrania segmentów W+X, lecz odziedziczone niższe mapowania firmware nie są przebudowywane przez kernel z egzekwowaną polityką W^X/NX.

## Pamięć

- Heap kernela jest statyczny i ma 2 MiB; nie może rosnąć.
- PMM wybiera jeden ciągły region użyteczny i śledzi najwyżej 1 048 576 ramek 4 KiB. Nie scala całej mapy UEFI.
- VMM kopiuje aktywny PML4 UEFI do prywatnej ramki, przełącza `CR3` i przechodzi runtime map/unmap self-test, lecz dziedziczy niższe tablice firmware zamiast tworzyć pełną przestrzeń kernela od zera.
- Backend aktywnego VMM zakłada identity mapping pamięci fizycznej UEFI, nie obsługuje LA57 i invaliduje TLB tylko lokalnym `invlpg`.
- Nie ma przestrzeni adresowych per proces, demand paging, copy-on-write, memory-mapped files, swap ani ochrony guard pages.
- Moduły nie mają kompletnej synchronizacji potrzebnej dla preempcji i SMP.

## Pliki i nośniki

- Shell i wszystkie jego mutacje nadal używają RAMFS; każdy restart usuwa te zmiany.
- Kernel montuje partycję `Kurogane Root` read-only przez AHCI, GPT, PartitionDevice, FAT32 i VFS oraz odczytuje `/etc/system.conf`.
- Nie ma FAT32 write, block cache, trwałego `sync`, deskryptorów plików per proces ani uprawnień.
- Limity RAMFS to 256 węzłów, 64 dzieci katalogu, 64 KiB na plik i 1 MiB logicznych danych.
- `cp` kopiuje tylko pojedynczy plik, a `cp`/`mv` nie nadpisują istniejącego celu.
- Nie ma instalatora ani trybu aktualizacji systemu na dysku.

## Urządzenia i platforma

- Framebuffer to GOP przekazany przez firmware; brak akceleracji i zmiany trybu po starcie.
- Wejście obsługuje klawiaturę PS/2. Nie ma myszy, USB ani touch.
- PCI rejestruje urządzenia w Device Managerze. AHCI read/write/flush jest zakwalifikowane na QEMU q35, ale nie na szerokim realnym hardware; brak NVMe, VirtIO, NIC i sterowników GPU.
- Używany jest legacy 8259 PIC i PIT; brak APIC/IOAPIC, HPET i SMP.
- RSDP jest przekazywany, ale brak pełnego ACPI. Restart i poweroff bazują na portach zależnych od emulatora.
- Serial służy do wyjściowych logów, nie jako niezależna konsola wejściowa.
- Nie przeprowadzono kwalifikacji na fizycznym sprzęcie.

## Sieć

- Parsery Ethernet/ARP/IPv4/ICMP i logika są testowane, lecz nie ma sterownika karty.
- Aktywnym interfejsem jest kolejka loopback; `net ping` potwierdza tylko `127.0.0.1`.
- Brak ruchu do QEMU, VirtualBox, hosta lub Internetu.
- Brak DHCP, DNS używanego przez kernel, TCP, socketów, firewalla i API sieciowego dla programów.
- Safe mode całkowicie pomija inicjalizację usługi sieciowej.

## Shell i użytkownicy

- Shell nie jest procesem i nie ma wielu sesji.
- `tasks` pokazuje callbacki schedulera; nie ma jeszcze polecenia `ps` ani tabeli procesów. `whoami` zwraca `kernel`.
- Brak użytkowników, logowania, grup, ACL i modelu uprawnień.
- Parser obsługuje podstawowe pojedyncze/podwójne cudzysłowy i escape. Brak pipes, redirection, job control, sygnałów, zmiennych środowiskowych, globów i skryptów.
- Brak `kill`, `mount`, `unmount`, package managera i wykonywania binariów.
- Historia obejmuje 16 linii w RAM i znika po restarcie.

## GUI i aplikacje

- `desktop`, `monitor`, `files` i `about` są pełnoekranowymi callbackami ring 0.
- Nie ma osobnego Terminala ani Settings.
- Nie ma myszy, kursora, kompozytora, menedżera okien, wielu powierzchni, focusu ani zmiany rozmiaru.
- `files` tylko listuje root RAMFS; nie jest pełnym menedżerem plików.
- Awarie widoku nie są izolowane od kernela.
- Safe mode nie rejestruje żadnego widoku.

## ABI i SDK

- Deskryptor ABI ma zero dostępnych funkcji i brak transportu.
- Projekty SDK są compile-only; nie można ich zlinkować ani uruchomić na KuroganeOS.
- Nie ma ABI sterowników, runtime, loadera ELF userspace ani stabilnej kompatybilności binarnej usług.

## Testy i niezawodność

- Testy hostowe dobrze pokrywają algorytmy, ale działają w procesie systemu hosta.
- QEMU sprawdza główny scenariusz startu, Device/Driver Manager, AHCI/GPT, read-only root FAT32/VFS, shell, RAMFS, loopback i safe mode.
- VirtualBox smoke test sprawdza jedynie osiągnięcie stabilnego promptu z ISO.
- Brak testów wielogodzinnych, fuzzingu wejścia sprzętowego, utraty zasilania, presji wielu procesów i realnego sprzętu.
- Tryb safe jest ograniczonym startem diagnostycznym, nie środowiskiem naprawy trwałego systemu.

## Priorytet dalszej pracy

Kolejność usuwania ograniczeń pozostaje architektoniczna: bezpieczny FAT32 write/cache/persistence, własna mapa kernela/HHDM i pełniejszy PMM, ochrona stron oraz synchronizacja, przełączanie kontekstu i ring 3, syscalle/FD, USB, sterownik NIC i ACPI. Dodawanie kolejnych atrap aplikacji przed tymi fundamentami zwiększałoby dług techniczny.

Uzasadnienie i szczegółowy plan znajdują się w [PROJECT_AUDIT.md](PROJECT_AUDIT.md).
